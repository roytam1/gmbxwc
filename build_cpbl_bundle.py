#!/usr/bin/env python
"""build_cpbl_bundle.py - pack the gmbxwc_dll.c inventory into one bundle file.

Usage:
    python build_cpbl_bundle.py [output]

Default output is gmbxwc_bundle.dat, the fixed name gmbxwc_ext.dll
auto-loads (DLL directory first, then current directory).

The inventory (codepage, blob file, display name) is read from g_tables[]
in gmbxwc_dll.c and stored in the bundle directory, so gmbxwc_ext.dll
needs no compiled-in table: no bundle (or a corrupt one) means 0 entries.
Adding a table with dll_add_cpbl.py and re-running this script ships it
in the bundle. Layout (all little-endian):

    magic 4s 'CPBX', version u32 (=1), count u32,
    count x (code_page u32, blob_offset u32, blob_size u32,
             blob_name char[32], display_name char[64]),
    raw CPBL blobs back to back.

Stock Python 3 only (struct, re).
"""

import os
import re
import struct
import sys

SRC = "gmbxwc_dll.c"
DEFAULT_OUT = "gmbxwc.dat"
MAGIC = b"CPBX"
VERSION = 1
NAME_LEN = 32
DISP_LEN = 64


def fail(msg):
    print("build_cpbl_bundle: error: " + msg, file=sys.stderr)
    sys.exit(1)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUT
    if len(sys.argv) > 2:
        print(__doc__)
        sys.exit(1)

    with open(SRC, "r", encoding="utf-8") as f:
        src = f.read()
    m = re.search(r"static const TableEntry g_tables\[\] = \{(.*?)\};", src, re.S)
    if not m:
        fail("%s: g_tables[] block not found" % SRC)
    entries = re.findall(r"\{\s*(\d+)\s*,\s*\"([^\"]*)\"\s*,\s*\"([^\"]*)\"\s*\}",
                         m.group(1))
    if not entries:
        fail("%s: no table entries parsed" % SRC)

    blobs = []
    for cp_s, blob_name, disp in entries:
        if not os.path.isfile(blob_name):
            fail("blob not found (run from the table directory): %s" % blob_name)
        with open(blob_name, "rb") as f:
            data = f.read()
        if len(data) < 40:
            fail("%s: smaller than CodePageHeader" % blob_name)
        magic, header_cp = struct.unpack("<4sI", data[:8])
        if magic not in (b"CPBL", b"GB18", b"C219"):
            fail("%s: bad magic %r" % (blob_name, magic))
        if header_cp != int(cp_s):
            fail("%s: header code_page=%d disagrees with inventory %s"
                 % (blob_name, header_cp, cp_s))
        try:
            name_enc = blob_name.encode("ascii")
        except UnicodeEncodeError:
            fail("%s: blob name must be ASCII" % blob_name)
        if len(name_enc) > NAME_LEN - 1:
            fail("%s: blob name too long for directory" % blob_name)
        disp_enc = disp.encode("utf-8")
        if len(disp_enc) > DISP_LEN - 1:
            fail("%s: display name too long for directory" % blob_name)
        blobs.append((int(cp_s), name_enc, disp_enc, data))

    hdr_len = 12
    rec_len = 12 + NAME_LEN + DISP_LEN
    off = hdr_len + len(blobs) * rec_len
    blob = bytearray()
    blob.extend(struct.pack("<4sII", MAGIC, VERSION, len(blobs)))
    for cp, name_enc, disp_enc, data in blobs:
        blob.extend(struct.pack("<III", cp, off, len(data)))
        blob.extend(name_enc + b"\0" * (NAME_LEN - len(name_enc)))
        blob.extend(disp_enc + b"\0" * (DISP_LEN - len(disp_enc)))
        off += len(data)
    for _cp, _name_enc, _disp_enc, data in blobs:
        blob.extend(data)

    with open(out_path, "wb") as f:
        f.write(blob)
    for i, (cp, name_enc, _disp_enc, data) in enumerate(blobs):
        print("[%2d] cp=%5d %-14s %7d bytes" % (i, cp, name_enc.decode("ascii"), len(data)))
    print("wrote %s (%d bytes, %d tables)" % (out_path, len(blob), len(blobs)))


if __name__ == "__main__":
    main()
