import sys
import struct

def parse_ucm(filename, is_gb18030):
    """Parses an ICU .ucm file into single-byte, double-byte, and 4-byte linear maps."""
    sbcs_map = {}
    dbcs_map = {}
    wc2mb = {}
    ext_b_mappings = {}
    dbcs_first_bytes = set()
    four_byte_list = []
    in_charmap = False

    with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'): 
                continue
            if line == "CHARMAP":
                in_charmap = True
                continue
            if line == "END CHARMAP":
                break
            
            if in_charmap and line.startswith('<U'):
                parts = line.split()
                if len(parts) < 2: 
                    continue

                # 1. Parse Unicode Codepoint string (handles <UXXXX> or <UXXXXXXXX>)
                uc_str = parts[0][2:-1]
                uc_val = int(uc_str, 16)

                # 2. Parse MultiByte hex notation sequence (\x81\x30\x81\x30)
                mb_str = parts[1]
                mb_bytes = [int(b, 16) for b in mb_str.split('\\x') if b]

                # 3. Parse ICU Precision state indicator (|0, |1, |2, |3)
                indicator = 0
                if len(parts) >= 3 and parts[2].startswith('|'):
                    indicator = int(parts[2][1])

                # Route to decoding structures (Roundtrip |0 or Fallback Decode |3)
                if indicator in (0, 3):
                    if len(mb_bytes) == 1:
                        sbcs_map[mb_bytes[0]] = uc_val
                    elif len(mb_bytes) == 2:
                        cp_val = (mb_bytes[0] << 8) | mb_bytes[1]
                        dbcs_map[cp_val] = uc_val
                        dbcs_first_bytes.add(mb_bytes[0])
                    elif len(mb_bytes) == 4 and is_gb18030:
                        b1, b2, b3, b4 = mb_bytes
                        # Compute the linear address coordinate space specified by the GB18030 standard
                        idx = (b1 - 0x81) * 12600 + (b2 - 0x30) * 1260 + (b3 - 0x81) * 10 + (b4 - 0x30)
                        four_byte_list.append((idx, uc_val))

                # Route to encoding structures (BMP/DBCS limits only for our 2-Tier Inverse Trie)
                if indicator in (0, 1):
                    if len(mb_bytes) <= 2:
                        val = mb_bytes[0] if len(mb_bytes) == 1 else (mb_bytes[0] << 8) | mb_bytes[1]

                        if uc_val <= 0xFFFF:
                            wc2mb[uc_val] = val
                        else:
                            # Captures Plane 2 / HKSCS Extension B mappings seamlessly
                            ext_b_mappings[uc_val] = val

    # Collapse sequential 4-byte mappings dynamically into continuous linear calculation equations
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

    return sbcs_map, dbcs_map, wc2mb, dbcs_first_bytes, gb_ranges, ext_b_mappings

def build_blob(ucm_path, cp_num, style):
    is_ebcdic = (style == "-ebcdic")
    is_gb18030 = (style == "-gb18030")
    
    sbcs_map, dbcs_map, wc2mb, dbcs_first_bytes, gb_ranges, ext_b_mappings = parse_ucm(ucm_path, is_gb18030)
    
    if is_gb18030: magic = b'GB18'
    elif is_ebcdic: magic = f"C{cp_num:03d}".encode('ascii')[:4]
    else: magic = b'CPBL'

    base_header_size = 40
    dbcs_lead_table = [0] * 256
    sbcs_table = [0] * 256
    trail_windows, trail_pool = [], []
    active_first_bytes = sorted(list(dbcs_first_bytes))
    
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
    off_dir = off_pool + (len(trail_pool) * 2)
    off_pages = off_dir + (len(page_directory) * 2)
    off_extra = off_pages + (len(unique_pages) * 512)
    extra_count = len(gb_ranges) if is_gb18030 else len(ext_b_mappings)
    wchar_dir_count = len(page_directory)

    blob = bytearray()
    blob.extend(struct.pack('<4sIIIIIIIII', magic, cp_num, off_windows, off_pool, off_dir, off_pages, off_extra, extra_count, 0, wchar_dir_count))
    
    if is_ebcdic:
        for val in sbcs_table: blob.extend(struct.pack('<H', val))
    for val in dbcs_lead_table: blob.extend(struct.pack('<H', val))
    for win in trail_windows: blob.extend(win)
    for val in trail_pool: blob.extend(struct.pack('<H', val))
    for idx in page_directory: blob.extend(struct.pack('<H', idx))
    for p in unique_pages:
        for val in p: blob.extend(struct.pack('<H', val))
        
    if is_gb18030:
        for start_i, end_i, start_uc in gb_ranges:
            blob.extend(struct.pack('<III', start_i, end_i, start_uc))
    else:
        sorted_ext_b = sorted(ext_b_mappings.items(), key=lambda x: x[0])
        # When writing out the 'off_extra' section payload:
        for uni, dbcs in sorted_ext_b:
            # Pack as 4-byte Unicode followed by 2-byte DBCS (padded to 4-bytes if alignment is needed)
            # Here we use standard 6-byte entries packed tightly:
            blob.extend(struct.pack('<IH', uni, dbcs))

    return blob

def main():
    if len(sys.argv) < 6:
        print("ICU UCM to CPBL Code Page Converter Tool")
        print("Usage: python build_cpbl_ucm.py <ucm_file> <output_file> -style [-bin|-c] <cp_id>")
        print("Styles:  -stateless  (Shift-JIS, Big5, etc.)")
        print("         -ebcdic     (IBM EBCDIC variants)")
        print("         -gb18030    (GB18030 range-compressed execution configuration)")
        sys.exit(1)
    
    ucm_file, out_file, style, out_flag, cp_id = sys.argv[1], sys.argv[2], sys.argv[3].lower(), sys.argv[4].lower(), int(sys.argv[5])
    
    print(f"Reading and analyzing {ucm_file}...")
    blob = build_blob(ucm_file, cp_id, style)
    
    if out_flag == "-c":
        with open(out_file, 'w', encoding='ascii') as f:
            f.write(f"/* Auto-generated from {ucm_file} for GB18030-2022 Setup */\n")
            f.write(f"const unsigned char cp{cp_id}_blob_data[{len(blob)}] = {{\n    ")
            for i, b in enumerate(blob):
                f.write(f"0x{b:02X}")
                if i < len(blob) - 1: f.write(", ")
                if (i + 1) % 12 == 0: f.write("\n    ")
            f.write("\n};\n")
        print(f"Successfully generated C header file array: {out_file} ({len(blob)} bytes)")
    else:
        with open(out_file, 'wb') as f: 
            f.write(blob)
        print(f"Successfully generated raw binary file asset: {out_file} ({len(blob)} bytes)")

if __name__ == '__main__': 
    main()
