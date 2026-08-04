#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
build_cpbl_nls.py
Compiles Windows NLS files directly into the unified 40-byte header CPBL specification.
"""

import sys
import struct

def parse_nls(nls_path):
    with open(nls_path, "rb") as f:
        header_raw = f.read(26)
        if len(header_raw) < 26:
            raise ValueError("Invalid or truncated NLS header.")
            
        wSize, code_page, max_char_size, def_char0, def_char1, uni_def_char, unk1, unk2 = struct.unpack('<HHHBBHHH', header_raw[:14])
        
        f.seek(wSize * 2)
        offset_to_unicode2cp = struct.unpack('<H', f.read(2))[0]
        primary_table = list(struct.unpack('<256H', f.read(512)))
        
        oem_table_size = struct.unpack('<H', f.read(2))[0]
        if oem_table_size > 0:
            f.seek(oem_table_size * 2, 1)
            
        num_dbcs_ranges = struct.unpack('<H', f.read(2))[0]
        
        sbcs_map = {}
        dbcs_map = {}
        dbcs_first_bytes = set()
        dbcs_leadbyte_offsets = [0] * 256
        
        if num_dbcs_ranges > 0:
            dbcs_leadbyte_offsets = list(struct.unpack('<256H', f.read(512)))
            num_subtables = sum(1 for x in dbcs_leadbyte_offsets if x != 0)
            subtables_words = list(struct.unpack(f'<{num_subtables * 256}H', f.read(num_subtables * 512)))
            
            for b in range(256):
                off = dbcs_leadbyte_offsets[b]
                if off != 0:
                    dbcs_first_bytes.add(b)
                    subtable_idx = off - 256
                    for t in range(256):
                        uc_val = subtables_words[subtable_idx + t]
                        if uc_val != 0:
                            dbcs_map[(b << 8) | t] = uc_val
                            
        for b in range(256):
            if dbcs_leadbyte_offsets[b] == 0:
                sbcs_map[b] = primary_table[b]

        # Inversion Table parsing (Unicode -> MultiByte)
        f.seek((wSize + 1 + offset_to_unicode2cp) * 2)
        wc2mb_map = {}
        
        if max_char_size == 1:
            u2cp_data = f.read(65536)
            for uc in range(65536):
                if u2cp_data[uc] != 0:
                    wc2mb_map[uc] = u2cp_data[uc]
        else:
            u2cp_data = f.read(131072)
            u2cp_words = struct.unpack('<65536H', u2cp_data)
            for mb, uc in sbcs_map.items():
                if uc != 0 and uc != uni_def_char: 
                    wc2mb_map[uc] = mb
            for mb, uc in dbcs_map.items():
                if uc != 0 and uc != uni_def_char:
                    if uc in wc2mb_map:
                        nls_val = u2cp_words[uc]
                        if nls_val == mb or (((nls_val & 0xFF) << 8) | (nls_val >> 8)) == mb:
                            wc2mb_map[uc] = mb
                    else:
                        wc2mb_map[uc] = mb

    return code_page, sbcs_map, dbcs_map, dbcs_first_bytes, wc2mb_map, def_char0

def build_unified_blob(nls_path):
    code_page, sbcs_map, dbcs_map, dbcs_first_bytes, wc2mb_map, defchar = parse_nls(nls_path)
    
    base_header_size = 40
    sbcs_table = [0] * 256
    dbcs_lead_table = [0] * 256
    trail_windows, trail_pool = [], []
    active_first_bytes = sorted(list(dbcs_first_bytes))
    
    # Dynamic 32-bit trail pool scanner (for supplementary planes)
    has_ext_b_in_dbcs = any(uc > 0xFFFF for uc in dbcs_map.values())
    is_32bit_pool = 1 if has_ext_b_in_dbcs else 0
    pool_stride = 4 if is_32bit_pool else 2

    # 1. Populate SBCS Table
    for i in range(256):
        sbcs_table[i] = sbcs_map.get(i, 0x0000)

    # 2. Map DBCS Lead Byte indices and construct trail windows
    for idx, fb in enumerate(active_first_bytes):
        dbcs_lead_table[fb] = 0x8000 | idx
        v_trails = [cp & 0xFF for cp in dbcs_map.keys() if (cp >> 8) == fb]
        min_t, max_t = (min(v_trails), max(v_trails)) if v_trails else (0, 0)
        pool_offset = len(trail_pool)
        trail_windows.append(struct.pack('<BBHI', min_t, max_t, 0, pool_offset))
        for t in range(min_t, max_t + 1):
            trail_pool.append(dbcs_map.get((fb << 8) | t, 0x0000))

    # 3. Compile compressed 2-tier WC2MB page tables
    pages = [[0] * 256 for _ in range(256)]
    for uc, cp in wc2mb_map.items():
        pages[uc >> 8][uc & 0xFF] = cp
        
    unique_pages, page_directory = [], []
    for p in pages:
        if p not in unique_pages:
            unique_pages.append(p)
        page_directory.append(unique_pages.index(p))

    # 4. Calculate absolute binary offsets (40B Header + 512B SBCS + 512B DBCS Lead)
    off_windows = base_header_size + 512 + 512
    off_pool = off_windows + (len(trail_windows) * 8)
    off_dir = off_pool + (len(trail_pool) * pool_stride)
    off_pages = off_dir + (len(page_directory) * 2)
    off_extra = off_pages + (len(unique_pages) * 512)

    # 5. Pack unified 40-byte header
    blob = bytearray()
    blob.extend(struct.pack('<4sIIIIIIIII', b'CPBL', code_page, off_windows, off_pool, off_dir, off_pages, off_extra, 0, is_32bit_pool, defchar))
    
    # 6. Payload Sections
    for val in sbcs_table: blob.extend(struct.pack('<H', val))
    for val in dbcs_lead_table: blob.extend(struct.pack('<H', val))
    for win in trail_windows: blob.extend(win)
    for val in trail_pool: blob.extend(struct.pack('<I' if is_32bit_pool else '<H', val))
    for idx in page_directory: blob.extend(struct.pack('<H', idx))
    for p in unique_pages:
        for val in p: blob.extend(struct.pack('<H', val))

    return code_page, blob

def main():
    if len(sys.argv) < 3:
        print("Usage: python build_cpbl_nls.py <input.nls> <output.cpbl>")
        sys.exit(1)
        
    cp_num, blob = build_unified_blob(sys.argv[1])
    with open(sys.argv[2], "wb") as f:
        f.write(blob)
    print(f"Unified CPBL structure successfully compiled for CP {cp_num} ({len(blob)} bytes).")

if __name__ == '__main__': 
    main()
