import sys
import struct
import csv

def parse_csv(filename, is_gb18030):
    sbcs_map = {}       # single-byte: int -> uc_val
    mb_map = {}         # multi-byte: tuple(bytes) -> uc_val (2, 3, 4 bytes)
    wc2mb = {}          # uc_val -> mb int representation (for 1 or 2 byte)
    ext_b_mappings = {} # uc_val > 0xFFFF -> mb int representation
    four_byte_list = [] # for GB18030 ranges

    with open(filename, 'r', encoding='utf-8') as f:
        reader = csv.reader(f)
        for row in reader:
            if not row or len(row) < 2: continue
            col1, col2 = row[0].strip().lower(), row[1].strip().lower()
            if any(x in col1 or x in col2 for x in ('multibyte', 'unicode', 'mb', 'wc')): continue
            
            try:
                mb_clean = col1.replace('0x', '').replace('\\x', '').replace(' ', '')
                wc_clean = col2.replace('0x', '').replace('\\x', '').replace(' ', '')
                uc_val = int(wc_clean, 16)
                mb_bytes = tuple(int(mb_clean[i:i+2], 16) for i in range(0, len(mb_clean), 2))
            except ValueError: continue

            if not mb_bytes: continue

            if len(mb_bytes) == 1:
                sbcs_map[mb_bytes[0]] = uc_val
            elif len(mb_bytes) >= 2:
                if len(mb_bytes) == 4 and is_gb18030:
                    b1, b2, b3, b4 = mb_bytes
                    idx = (b1 - 0x81) * 12600 + (b2 - 0x30) * 1260 + (b3 - 0x81) * 10 + (b4 - 0x30)
                    four_byte_list.append((idx, uc_val))
                else:
                    mb_map[mb_bytes] = uc_val

            if len(mb_bytes) <= 2:
                val = mb_bytes[0] if len(mb_bytes) == 1 else (mb_bytes[0] << 8) | mb_bytes[1]
                if uc_val <= 0xFFFF:
                    wc2mb[uc_val] = val
                else:
                    ext_b_mappings[uc_val] = val

    gb_ranges = []
    if four_byte_list:
        four_byte_list.sort(key=lambda x: x[0])
        start_idx, start_uc = four_byte_list[0]
        curr_idx, curr_uc = start_idx, start_uc
        for idx, uc in four_byte_list[1:]:
            if idx == curr_idx + 1 and uc == curr_uc + 1:
                curr_idx, curr_uc = idx, uc
            else:
                gb_ranges.append((start_idx, curr_idx, start_uc))
                start_idx, start_uc = idx, uc
                curr_idx, curr_uc = idx, uc
        gb_ranges.append((start_idx, curr_idx, start_uc))

    return sbcs_map, mb_map, wc2mb, gb_ranges, ext_b_mappings

def build_blob(csv_path, cp_num, style, defchar):
    is_ebcdic = (style == "-ebcdic")
    is_gb18030 = (style == "-gb18030")
    
    sbcs_map, mb_map, wc2mb, gb_ranges, ext_b_mappings = parse_csv(csv_path, is_gb18030)
    
    if is_gb18030: magic = b'GB18'
    elif is_ebcdic: magic = f"C{cp_num:03d}".encode('ascii')[:4]
    else: magic = b'CPBL'

    base_header_size = 40
    sbcs_table = [0] * 256
    dbcs_lead_table = [0] * 256
    trail_windows = []
    trail_pool = []
    
    has_ext_b_in_dbcs = any(uc > 0xFFFF for uc in mb_map.values())
    is_32bit_pool = 1 if has_ext_b_in_dbcs else 0
    pool_stride = 4 if is_32bit_pool else 2

    # Populate SBCS Table
    for i in range(256):
        sbcs_table[i] = sbcs_map.get(i, 0x0000)

    # Group multi-byte mappings by lead byte (b1)
    lead_groups = {}
    for mb_bytes, uc in mb_map.items():
        b1 = mb_bytes[0]
        if b1 not in lead_groups:
            lead_groups[b1] = []
        lead_groups[b1].append((mb_bytes[1:], uc))

    # Helper function: Recursively builds intermediate & leaf trail windows (Trie)
    def compile_level_into_slot(suffix_list, slot_idx):
        current_bytes = set(s[0][0] for s in suffix_list)
        min_trail = min(current_bytes)
        max_trail = max(current_bytes)
        range_len = max_trail - min_trail + 1
        
        is_leaf = (len(suffix_list[0][0]) == 1)

        if is_leaf:
            pool_start_idx = len(trail_pool)
            trail_pool.extend([0] * range_len)
            
            for remaining, uc in suffix_list:
                b_trail = remaining[0]
                trail_pool[pool_start_idx + (b_trail - min_trail)] = uc
            
            # Action Type = 0 (Leaf Node)
            trail_windows[slot_idx] = struct.pack('<BBHI', min_trail, max_trail, 0, pool_start_idx)
        else:
            by_byte = {}
            for remaining, uc in suffix_list:
                b_curr = remaining[0]
                if b_curr not in by_byte:
                    by_byte[b_curr] = []
                by_byte[b_curr].append((remaining[1:], uc))

            base_child_idx = len(trail_windows)
            dummy_win = b'\x00' * 8
            for _ in range(range_len):
                trail_windows.append(dummy_win)

            # Action Type = 2 (Intermediate Node; pool_offset points to child window index)
            trail_windows[slot_idx] = struct.pack('<BBHI', min_trail, max_trail, 2, base_child_idx)

            for b_curr, sub_suffixes in by_byte.items():
                child_slot = base_child_idx + (b_curr - min_trail)
                compile_level_into_slot(sub_suffixes, child_slot)

    if is_ebcdic:
        for i in range(256): dbcs_lead_table[i] = 0xFFFF
        active_first_bytes = sorted(list(lead_groups.keys()))
        for idx, fb in enumerate(active_first_bytes):
            dbcs_lead_table[fb] = idx
            # Simple 2-byte compilation for EBCDIC
            v_trails = [s[0][0] for s in lead_groups[fb]]
            min_t, max_t = min(v_trails), max(v_trails)
            pool_offset = len(trail_pool)
            trail_windows.append(struct.pack('<BBHI', min_t, max_t, 0, pool_offset))
            for t in range(min_t, max_t + 1):
                uc = next((s[1] for s in lead_groups[fb] if s[0][0] == t), 0x0000)
                trail_pool.append(uc)
    else:
        for b1 in range(256):
            if b1 in lead_groups:
                root_slot = len(trail_windows)
                trail_windows.append(b'\x00' * 8)
                dbcs_lead_table[b1] = 0x8000 | root_slot
                compile_level_into_slot(lead_groups[b1], root_slot)
            elif is_gb18030 and 0x81 <= b1 <= 0xFE:
                dbcs_lead_table[b1] = 0x8000

    # WC2MB Page Tables construction
    pages = [[0] * 256 for _ in range(256)]
    for uc, cp in wc2mb.items(): pages[uc >> 8][uc & 0xFF] = cp
    unique_pages, page_directory = [], []
    for p in pages:
        if p not in unique_pages: unique_pages.append(p)
        page_directory.append(unique_pages.index(p))

    # Calculate Header Offsets (SBCS table 512B + Lead table 512B)
    off_windows = base_header_size + 512 + 512
    off_pool = off_windows + (len(trail_windows) * 8)
    off_dir = off_pool + (len(trail_pool) * pool_stride)
    off_pages = off_dir + (len(page_directory) * 2)
    off_extra = off_pages + (len(unique_pages) * 512)
    extra_count = len(gb_ranges) if is_gb18030 else len(ext_b_mappings)

    blob = bytearray()
    # Header format: magic, cp, windows, pool, dir, pages, extra, count, is_32bit_pool, defchar
    blob.extend(struct.pack('<4sIIIIIIIII', magic, cp_num, off_windows, off_pool, off_dir, off_pages, off_extra, extra_count, is_32bit_pool, defchar))
    
    # Payload sections
    for val in sbcs_table: blob.extend(struct.pack('<H', val))
    for val in dbcs_lead_table: blob.extend(struct.pack('<H', val))
    for win in trail_windows: blob.extend(win)
    
    for val in trail_pool: 
        blob.extend(struct.pack('<I' if is_32bit_pool else '<H', val))
        
    for idx in page_directory: blob.extend(struct.pack('<H', idx))
    for p in unique_pages:
        for val in p: blob.extend(struct.pack('<H', val))
        
    if is_gb18030:
        for start_i, end_i, start_uc in gb_ranges:
            blob.extend(struct.pack('<III', start_i, end_i, start_uc))
    else:
        sorted_ext_b = sorted(ext_b_mappings.items(), key=lambda x: x[0])
        for uni, dbcs in sorted_ext_b:
            blob.extend(struct.pack('<IH', uni, dbcs))

    return blob

def main():
    if len(sys.argv) < 7:
        print("Usage: python build_cpbl_csv.py <csv_file> <output_file> -style [-bin|-c] <cp_id> <def_char>")
        sys.exit(1)
    
    csv_file, out_file, style, out_flag, cp_id, def_char = sys.argv[1], sys.argv[2], sys.argv[3].lower(), sys.argv[4].lower(), int(sys.argv[5]), int(sys.argv[6], 0)
    blob = build_blob(csv_file, cp_id, style, def_char)
    
    if out_flag == "-c":
        with open(out_file, 'w') as f:
            f.write(f"const unsigned char cp{cp_id}_blob_data[{len(blob)}] = {{\n ")
            for i, b in enumerate(blob):
                f.write(f"0x{b:02X}, " + ("\n " if (i+1)%12==0 else ""))
            f.write("\n};\n")
    else:
        with open(out_file, 'wb') as f: f.write(blob)

if __name__ == '__main__': main()
