#!/usr/bin/env python
"""dll_add_cpbl.py - append a new CPBL table to the gmbxwc.dll sources and rebuild.

Usage:
    python dll_add_cpbl.py <table.DAT> <codepage> "<display name>" [--no-build]

Example:
    python dll_add_cpbl.py CP957.DAT 957 "Johab (CP957)"

This appends one entry to g_tables[] in gmbxwc_dll.c and one resource line
to gmbxwc.rc (ID = 1000 + dense index, append-only: existing order and IDs
are never touched), re-packs gmbxwc_bundle.dat (inventory is read from
gmbxwc_dll.c, so gmbxwc_ext.dll picks the table up with no source change),
then rebuilds gmbxwc.dll + libgmbxwc.a like mkdll.bat.

Stock Python 3 only (struct, re, shutil, subprocess, ctypes).
"""

import ctypes
import os
import re
import shutil
import struct
import subprocess
import sys

RES_BASE = 1000
DLL_C = "gmbxwc_dll.c"
RC_FILE = "gmbxwc.rc"
KNOWN_MAGICS = (b"CPBL", b"GB18", b"C219")


def fail(msg):
    print("dll_add_cpbl: error: " + msg, file=sys.stderr)
    sys.exit(1)


def c_escape(s):
    if any(ord(c) < 0x20 or ord(c) == 0x7F for c in s):
        fail("display name must not contain control characters")
    return s.replace("\\", "\\\\").replace('"', '\\"')


def read_header(dat_path):
    with open(dat_path, "rb") as f:
        raw = f.read(40)
    if len(raw) < 40:
        fail("%s: file smaller than 40-byte CodePageHeader" % dat_path)
    magic, code_page = struct.unpack("<4sI", raw[:8])
    if magic not in KNOWN_MAGICS:
        fail("%s: unknown magic %r (expected CPBL/GB18/C219)" % (dat_path, magic))
    return magic, code_page


def parse_tables_block(text):
    m = re.search(r"static const TableEntry g_tables\[\] = \{(.*?)\};", text, re.S)
    if not m:
        fail("%s: g_tables[] block not found" % DLL_C)
    return m


def parse_c_entries(block):
    return re.findall(r"\{\s*(\d+)\s*,\s*\"([^\"]*)\"\s*,\s*\"([^\"]*)\"\s*\}", block)


def parse_rc_entries(text):
    return re.findall(r"(?m)^(\d+)\s+CPBL\s+\"([^\"]+)\"", text)


def main():
    args = [a for a in sys.argv[1:] if a != "--no-build"]
    do_build = len(args) == len(sys.argv[1:])
    if len(args) != 3:
        print(__doc__)
        sys.exit(1)
    dat_arg, cp_arg, display = args
    try:
        code_page = int(cp_arg, 0)
    except ValueError:
        fail("codepage must be an integer, got %r" % cp_arg)
    if not display:
        fail("display name must not be empty")

    # 1. Stage the DAT next to the sources (windres resolves relative paths).
    blob_name = os.path.basename(dat_arg)
    if not os.path.isfile(dat_arg):
        fail("table not found: %s" % dat_arg)
    if os.path.abspath(dat_arg) != os.path.abspath(blob_name):
        shutil.copyfile(dat_arg, blob_name)
        print("copied %s -> %s" % (dat_arg, blob_name))
    magic, header_cp = read_header(blob_name)
    if header_cp != code_page:
        fail("%s header code_page=%d disagrees with argument %d"
             % (blob_name, header_cp, code_page))

    # 2. Parse current inventory and cross-check the two files agree.
    with open(DLL_C, "r", encoding="utf-8") as f:
        dll_text = f.read()
    with open(RC_FILE, "r", encoding="utf-8") as f:
        rc_text = f.read()
    block = parse_tables_block(dll_text)
    c_entries = parse_c_entries(block.group(1))
    rc_entries = parse_rc_entries(rc_text)
    if len(c_entries) != len(rc_entries):
        fail("%s has %d tables but %s has %d; fix the mismatch by hand"
             % (DLL_C, len(c_entries), RC_FILE, len(rc_entries)))
    for i, ((cp, blob, _disp), (rid, rblob)) in enumerate(zip(c_entries, rc_entries)):
        if int(rid) != RES_BASE + i or rblob != blob:
            fail("order/ID mismatch at index %d (%s vs %s:%s); fix by hand"
                 % (i, blob, rid, rblob))
    if any(int(cp) == code_page for cp, _b, _d in c_entries):
        fail("codepage %d already embedded" % code_page)
    if any(b == blob_name for _c, b, _d in c_entries):
        fail("blob %s already embedded" % blob_name)

    index = len(c_entries)
    res_id = RES_BASE + index

    # 3. Append to g_tables[] (last entry gains a comma; nothing else moves).
    last = None
    for m in re.finditer(r"\{\s*\d+\s*,\s*\"[^\"]*\"\s*,\s*\"[^\"]*\"\s*\}",
                         block.group(1)):
        last = m
    new_entry = '{ %d, "%s", "%s" }' % (code_page, blob_name, c_escape(display))
    abs_start = block.start(1) + last.start()
    abs_end = block.start(1) + last.end()
    dll_text = (dll_text[:abs_start] + last.group(0) + ",\n    " + new_entry
                + dll_text[abs_end:])
    dll_text, n = re.subn(r"Embeds all \d+ CPBL tables",
                          "Embeds all %d CPBL tables" % (index + 1), dll_text)
    if n != 1:
        fail("%s: header comment count not found" % DLL_C)
    with open(DLL_C, "w", encoding="utf-8", newline="") as f:
        f.write(dll_text)

    # 4. Append the resource line.
    if not rc_text.endswith("\n"):
        rc_text += "\n"
    rc_text += '%d CPBL "%s"\n' % (res_id, blob_name)
    rc_text, n = re.subn(r"embeds all \d+ CPBL tables",
                         "embeds all %d CPBL tables" % (index + 1), rc_text)
    if n != 1:
        fail("%s: header comment count not found" % RC_FILE)
    with open(RC_FILE, "w", encoding="utf-8", newline="") as f:
        f.write(rc_text)
    print("index %d: cp=%d blob=%s res_id=%d (%s)"
          % (index, code_page, blob_name, res_id, magic.decode("ascii")))

    # 5. Re-pack the external bundle (needs no compiler).
    if do_build:
        cmd = [sys.executable, "build_cpbl_bundle.py"]
        print("+ " + " ".join(cmd))
        subprocess.check_call(cmd)

    # 6. Rebuild (same commands as mkdll.bat).
    if do_build:
        windres = shutil.which("windres")
        gcc = shutil.which("gcc")
        if not windres or not gcc:
            print("dll_add_cpbl: sources updated but windres/gcc not on PATH;",
                  "run mkdll.bat after adding C:\\msys64\\mingw64\\bin to PATH",
                  file=sys.stderr)
            sys.exit(2)
        for cmd in ([windres, RC_FILE, "-o", "gmbxwc_res.o"],
                    [gcc, "-DGMBXWC_BUILD_DLL", "-shared", "-o", "gmbxwc.dll",
                     "gmbxwc.c", "gmbxwc_dll.c", "gmbxwc_res.o",
                     "-Wl,--out-implib,libgmbxwc.a"]):
            print("+ " + " ".join(cmd))
            subprocess.check_call(cmd)
        print("rebuilt gmbxwc.dll + libgmbxwc.a")
        if sys.platform == "win32":
            try:
                dll = ctypes.WinDLL(os.path.abspath("gmbxwc.dll"))
                dll.CodePage_EmbeddedCount.restype = ctypes.c_ulong
                got = dll.CodePage_EmbeddedCount()
                print("smoke: CodePage_EmbeddedCount() = %d" % got)
                if got != index + 1:
                    fail("smoke check failed (DLL reports %d)" % got)
            except OSError as e:
                print("dll_add_cpbl: warning: smoke check failed: %s" % e,
                      file=sys.stderr)

    print("done. Also append the new index to the order list in AGENTS.md.")


if __name__ == "__main__":
    main()
