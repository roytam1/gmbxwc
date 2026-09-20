# gmbxwc

A text encoding conversion library (MBCS ↔ UTF-16) written in **C89** for Windows,
plus Python table compilers and C test tools. Conversion tables are compiled from
authoritative sources (Unicode consortium CSVs, ICU `.ucm` files, Windows `.nls`
files) into a compact binary blob format, driven by one shared engine with three
modes: stateless DBCS/trie, GB18030, and stateful EBCDIC (SO/SI switching).

Windows-only: `wchar_t` is UTF-16LE and the headers use `windows.h` (`BOOL`, etc.).

## Supported codepages (dense index → table)

| Idx | CP | Description | Idx | CP | Description |
|---|---|---|---|---|---|
| 0 | 932 | Shift-JIS | 10 | 21935 | IBM EBCDIC Simp-Chinese Host |
| 1 | 936 | GBK | 11 | 21932 | EUC-JP |
| 2 | 949 | EUC-KR | 12 | 21950 | EUC-TW |
| 3 | 950 | Big5 (UAO 2.50) | 13 | 20000 | Trad. Chinese EUC-TW/CNS-MS |
| 4 | 951 | Big5-HKSCS-2008 | 14 | 20001 | Trad. Chinese TCA |
| 5 | 54936 | GB18030 | 15 | 20002 | Trad. Chinese ETen |
| 6 | 21937 | IBM EBCDIC Trad-Chinese Host | 16 | 20003 | Trad. Chinese IBM5550 |
| 7 | 21939 | IBM EBCDIC Japanese Latin-Kanji | 17 | 20004 | Trad. Chinese Teletext |
| 8 | 21930 | IBM EBCDIC Japanese Katakana-Kanji | 18 | 20005 | Trad. Chinese Wang |
| 9 | 21933 | IBM EBCDIC Korean | 19 | 20932 | ENC-JP-MS |

Order is append-only — never reorder, or existing index users break.

## Quick start: converting files

```bat
gcc -o gmb2wc.exe gmb2wc.c gmbxwc.c
gcc -o gwc2mb.exe gwc2mb.c gmbxwc.c

gmb2wc CP950.DAT big5.txt out-utf16.txt      :: MB -> UTF-16LE (writes BOM)
gwc2mb CP950.DAT out-utf16.txt back-big5.txt :: UTF-16LE -> MB (skips input BOM)
cpbldump CP950.DAT                            :: inspect a table blob
```

Sample fixtures: `106z*.txt` (multibyte inputs), `106z-wc.txt` (UTF-16 input).

## Using the library

Core engine (`gmbxwc.c` / `gmbxwc.h`, static link — just add `gmbxwc.c` to your build):

```c
CodePageContext *ctx = InitCodePageConverter(blob_bytes); /* you keep blob alive */
if (ctx && ctx->is_valid) {
    unsigned long need = CodePage_MB2WC(ctx, src, srclen, NULL, 0, NULL);
    /* ... allocate need wchar_t ... */
    CodePage_MB2WC(ctx, src, srclen, dest, need, &unmapped); /* U+FFFD on unmapped */
    CodePage_WC2MB(ctx, wsrc, wsrclen, mdest, mneed, &unmapped); /* def_char on unmapped */
}
FreeCodePageConverter(ctx); /* frees ctx only, not the blob */
```

Three ways to consume the DLLs, which hide blobs entirely behind the dense index:

1. **Import library** — `#include "gmbxwc_import.h"`, link `libgmbxwc.a` (MinGW)
   or build an import lib from `gmbxwc.def` (MSVC).
2. **Run-time linking** — `#include "gmbxwc_dynload.h"`, `LoadLibrary` +
   `GmbxwcApi_Load(hDll, &api)`, then call through `api.*`. Free ctxs before
   `FreeLibrary`.
3. Indexed one-shots for simple cases: `CodePage_IndexedMB2WC(idx, …)` /
   `CodePage_IndexedWC2MB(idx, …)` (fresh ctx per call; prefer
   `CreateConverter` + `MB2WC`/`WC2MB` in loops).

Discovery: `CodePage_EmbeddedCount()`, `CodePage_EmbeddedInfo(idx, &info)`
(`code_page` + `blob_name` + `display_name`), `CodePage_FindIndexForCodePage(932, &idx)`.

Two DLL flavors share this API — pick one per binary (never link both glues together):

- **`gmbxwc.dll`** — all tables embedded as `CPBL` resources. Build: `mkdll.bat`
  (MSVC) or `mkdll-mingw.bat` (`windres` + `gcc -shared`, also makes `libgmbxwc.a`).
- **`gmbxwc_ext.dll`** — same API, tables + inventory loaded from a single external
  `gmbxwc.dat` (DLL directory, then CWD; auto-loaded on first use).
  Build: `mkext.bat`, then `python build_cpbl_bundle.py`. No bundle (or a corrupt
  one) means `EmbeddedCount() == 0` and converting entry points return NULL/0.

## Building tables from sources

`mkdat.bat` is the source of truth — one line per table:

```bat
python build_cpbl_csv.py <in.TXT> <out.DAT> -stateless -bin <cp_id> <def_char>
python build_cpbl_ucm.py <in.ucm> <out.DAT> {-stateless|-ebcdic|-gb18030} -bin <cp_id> <def_char>
python build_cpbl_nls.py <in.nls> <out.dat>      :: cp + defchar come from the NLS header
```

`-bin` writes the raw blob (`-c` emits a C array, rarely used). `def_char` is the
WC→MB fallback (`0x3f` normally, `0x6f` for EBCDIC). After changing a generator,
re-run `mkdat.bat` and sanity-check with `cpbldump` plus a round-trip.

## Adding a new codepage

```bat
python dll_add_cpbl.py <table.DAT> <codepage> "<display name>" [--no-build]
```

Validates the blob header, appends to `g_tables[]` + `gmbxwc.rc` (append-only),
re-packs `gmbxwc.dat`, rebuilds, and smoke-checks the count. Then extend
the index table above and in `AGENTS.md`.

## Blob format (summary)

40-byte LE header (`magic` `'CPBL'`/`'GB18'`/`'C219'`, `code_page`, section offsets,
`extra_count`, `is_32bit_pool`, `def_char`), then: SBCS table → lead table →
trail-window trie → trail pool (u16, u32 if non-BMP entries exist) → WC→MB
2-tier page directory → extra table (`ExtBMapping` list, or `GB18030Range` list;
GB18030 supplementary plane U+10000–U+10FFFF is algorithmic). See `AGENTS.md`
for the full layout and engine notes.

## Requirements

- Windows, C89-clean code (`gcc -std=c89 -pedantic -Wall` warning-free).
- MSVC (`cl`, `rc`, `lib`) for `mkdll.bat` / `mkext.bat`, or MinGW
  (`gcc`, `windres` in `C:\msys64\mingw64\bin`) for the `-mingw` recipes.
- Stock Python 3 only for the table/bundle scripts (`struct`, `csv`, `re`).
- `AGENTS.md` holds the machine-precise contract (API, layouts, ground truths);
  keep it in sync when behavior changes.
