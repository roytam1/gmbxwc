import sys
import struct
import csv

def parse_csv(filename, is_gb18030):
    sbcs_map, dbcs_map, wc2mb = {}, {}, {}
    dbcs_first_bytes = set()
    four_byte_list = []

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
                mb_bytes = [int(mb_clean[i:i+2], 16) for i in range(0, len(mb_clean), 2)]
            except ValueError: continue

            if len(mb_bytes) == 1:
                sbcs_map[mb_bytes[0]] = uc_val
            elif len(mb_bytes) == 2:
                cp_val = (mb_bytes[0] << 8) | mb_bytes[1]
                dbcs_map[cp_val] = uc_val
                dbcs_first_bytes.add(mb_bytes[0])
            elif len(mb_bytes) == 4 and is_gb18030:
                b1, b2, b3, b4 = mb_bytes
                idx = (b1 - 0x81) * 12600 + (b2 - 0x30) * 1260 + (b3 - 0x81) * 10 + (b4 - 0x30)
                four_byte_list.append((idx, uc_val))

            if uc_val <= 0xFFFF and len(mb_bytes) <= 2:
                wc2mb[uc_val] = mb_bytes[0] if len(mb_bytes) == 1 else (mb_bytes[0] << 8) | mb_bytes[1]

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

    return sbcs_map, dbcs_map, wc2mb, dbcs_first_bytes, gb_ranges

def build_blob(csv_path, cp_num, style):
    is_ebcdic = (style == "-ebcdic")
    is_gb18030 = (style == "-gb18030")
    
    sbcs_map, dbcs_map, wc2mb, dbcs_first_bytes, gb_ranges = parse_csv(csv_path, is_gb18030)
    
    if is_gb18030: magic = b'GB18'
    elif is_ebcdic: magic = f"C{cp_num:03d}".encode('ascii')[:4]
    else: magic = b'CPBL'

    # Fixed 36-Byte Header Layout for strict 4-byte boundaries
    base_header_size = 36
    dbcs_lead_table = [0] * 256
    sbcs_table = [0] * 256
    trail_windows, trail_pool = [], []
    active_first_bytes = sorted(list(dbcs_first_bytes))
    
    # Critical Fix: Detect if any double-byte sequences map outside the BMP plane
    has_ext_b_in_dbcs = any(uc > 0xFFFF for uc in dbcs_map.values())
    is_32bit_pool = 1 if has_ext_b_in_dbcs else 0
    pool_stride = 4 if is_32bit_pool else 2

    if is_ebcdic:
        for i in range(256): sbcs_table[i] = sbcs_map.get(i, 0x0000)
        for i in range(256): dbcs_lead_table[i] = 0xFFFF
        for idx, fb in enumerate(active_first_bytes): dbcs_lead_table[fb] = idx
    else:
        for i in range(256):
            if i in sbcs_map: dbcs_lead_table[i] = sbcs_map[i] & 0x7FFF
            elif i in dbcs_first_bytes or (is_gb18030 and 0x81 <= i <= 0xFE): dbcs_lead_table[i] = 0x8000
        for idx, fb in enumerate(active_first_bytes): dbcs_lead_table[fb] |= idx

    for fb in active_first_bytes:
        v_trails = [cp & 0xFF for cp in dbcs_map.keys() if (cp >> 8) == fb]
        min_t, max_t = min(v_trails) if v_trails else 0, max(v_trails) if v_trails else 0
        pool_offset = len(trail_pool)
        trail_windows.append(struct.pack('<BBHI', min_t, max_t, 0, pool_offset))
        for t in range(min_t, max_t + 1): trail_pool.append(dbcs_map.get((fb << 8) | t, 0x0000))

    pages = [[0] * 256 for _ in range(256)]
    for uc, cp in wc2mb.items(): pages[uc >> 8][uc & 0xFF] = cp
    unique_pages, page_directory = [], []
    for p in pages:
        if p not in unique_pages: unique_pages.append(p)
        page_directory.append(unique_pages.index(p))

    off_windows = base_header_size + (512 if is_ebcdic else 0) + 512
    off_pool = off_windows + (len(trail_windows) * 8)
    off_dir = off_pool + (len(trail_pool) * pool_stride)
    off_pages = off_dir + (len(page_directory) * 2)
    off_extra = off_pages + (len(unique_pages) * 512)
    extra_count = len(gb_ranges) if is_gb18030 else 0

    blob = bytearray()
    # Header format: magic, cp, windows, pool, dir, pages, extra, count, is_32bit_pool
    blob.extend(struct.pack('<4sIIIIIIII', magic, cp_num, off_windows, off_pool, off_dir, off_pages, off_extra, extra_count, is_32bit_pool))
    
    if is_ebcdic:
        for val in sbcs_table: blob.extend(struct.pack('<H', val))
    for val in dbcs_lead_table: blob.extend(struct.pack('<H', val))
    for win in trail_windows: blob.extend(win)
    
    # Write using 32-bit or 16-bit format dynamically
    for val in trail_pool: 
        blob.extend(struct.pack('<I' if is_32bit_pool else '<H', val))
        
    for idx in page_directory: blob.extend(struct.pack('<H', idx))
    for p in unique_pages:
        for val in p: blob.extend(struct.pack('<H', val))
        
    if is_gb18030:
        for start_i, end_i, start_uc in gb_ranges:
            blob.extend(struct.pack('<III', start_i, end_i, start_uc))

    return blob

def main():
    if len(sys.argv) < 6:
        print("Usage: python build_cpbl_csv.py <csv_file> <output_file> -style [-bin|-c] <cp_id>")
        sys.exit(1)
    
    csv_file, out_file, style, out_flag, cp_id = sys.argv[1], sys.argv[2], sys.argv[3].lower(), sys.argv[4].lower(), int(sys.argv[5])
    blob = build_blob(csv_file, cp_id, style)
    
    if out_flag == "-c":
        with open(out_file, 'w') as f:
            f.write(f"const unsigned char cp{cp_id}_blob_data[{len(blob)}] = {{\n ")
            for i, b in enumerate(blob):
                f.write(f"0x{b:02X}, " + ("\n " if (i+1)%12==0 else ""))
            f.write("\n};\n")
    else:
        with open(out_file, 'wb') as f: f.write(blob)

if __name__ == '__main__': main()
