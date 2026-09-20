/* gmbxwc_dll.c - gmbxwc.dll resource glue (C89).
 *
 * Embeds all 20 CPBL tables as Windows resources (see gmbxwc.rc) and
 * exposes them through a dense 0-based index API declared in gmbxwc.h.
 * The engine itself stays in gmbxwc.c; this file only resolves blobs
 * from resources and forwards to InitCodePageConverter / CodePage_MB2WC /
 * CodePage_WC2MB.
 *
 * Resource blobs live as long as the DLL is loaded; contexts created by
 * CodePage_CreateConverter point into them, so free every ctx with
 * FreeCodePageConverter BEFORE unloading the DLL.
 */
#include "gmbxwc.h"

static HINSTANCE g_hGmbxwcInstance = NULL;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hGmbxwcInstance = hinstDLL;
        /*DisableThreadLibraryCalls(hinstDLL);*/
    }
    return TRUE;
}

#define GMBXWC_RES_BASE 1000

/* Inventory entry: dense index is the position in this table. */
typedef struct {
    unsigned long code_page;
    const char* blob_name;
    const char* display_name;
} TableEntry;

static const TableEntry g_tables[] = {
    { 932,   "CP932.DAT",   "Shift-JIS (CP932)" },
    { 936,   "CP936.DAT",   "GBK (CP936)" },
    { 949,   "CP949.DAT",   "EUC-KR (CP949)" },
    { 950,   "CP950.DAT",   "Big5 UAO 2.50 (CP950)" },
    { 951,   "HKSCS2K8.DAT","Big5-HKSCS-2008 (CP951)" },
    { 54936, "GB18030.DAT", "GB18030 (CP54936)" },
    { 21937, "IBM937.DAT",  "IBM EBCDIC Trad-Chinese Host (CP21937)" },
    { 21939, "IBM939.DAT",  "IBM EBCDIC Japanese Latin-Kanji (CP21939)" },
    { 21930, "IBM930.DAT",  "IBM EBCDIC Japanese Katakana-Kanji (CP21930)" },
    { 21933, "IBM933.DAT",  "IBM EBCDIC Korean (CP21933)" },
    { 21935, "IBM935.DAT",  "IBM EBCDIC Simp-Chinese Host (CP21935)" },
    { 21932, "EUCJP.DAT",   "EUC-JP (CP21932)" },
    { 21950, "EUCTW.DAT",   "EUC-TW (CP21950)" },
    { 20000, "c_20000.dat", "Chinese Traditional EUC-TW/CNS-MS (CP20000)" },
    { 20001, "c_20001.dat", "Chinese Traditional TCA (CP20001)" },
    { 20002, "c_20002.dat", "Chinese Traditional ETen (CP20002)" },
    { 20003, "c_20003.dat", "Chinese Traditional IBM5550 (CP20003)" },
    { 20004, "c_20004.dat", "Chinese Traditional Teletext (CP20004)" },
    { 20005, "c_20005.dat", "Chinese Traditional Wang (CP20005)" },
    { 20932, "c_20932.dat", "ENC-JP-MS (CP20932)" }
};

#define GMBXWC_TABLE_N (sizeof(g_tables) / sizeof(g_tables[0]))

/* Resolve the locked resource bytes for a dense index. Returns NULL on error. */
static const unsigned char* LockedBlobByIndex(unsigned long index, unsigned long* p_size) {
    HRSRC hrsrc;
    HGLOBAL hglob;
    DWORD size;
    const unsigned char* ptr;
    const CodePageHeader* h;

    if (p_size) {
        *p_size = 0;
    }
    if (index >= GMBXWC_TABLE_N) {
        return NULL;
    }
    if (g_hGmbxwcInstance == NULL) {
        return NULL;
    }
    hrsrc = FindResourceA(g_hGmbxwcInstance,
                          MAKEINTRESOURCEA(GMBXWC_RES_BASE + index),
                          "CPBL");
    if (hrsrc == NULL) {
        return NULL;
    }
    size = SizeofResource(g_hGmbxwcInstance, hrsrc);
    if (size < (DWORD)sizeof(CodePageHeader)) {
        return NULL;
    }
    hglob = LoadResource(g_hGmbxwcInstance, hrsrc);
    if (hglob == NULL) {
        return NULL;
    }
    ptr = (const unsigned char*)LockResource(hglob);
    if (ptr == NULL) {
        return NULL;
    }
    /* Light validation: known magic + expected codepage. No copy; no unlock/free needed. */
    h = (const CodePageHeader*)ptr;
    if (h->magic != 0x4C425043UL && h->magic != 0x38314247UL && h->magic != 0x39313243UL) {
        return NULL;
    }
    if (h->code_page != g_tables[index].code_page) {
        return NULL;
    }
    if (p_size) {
        *p_size = (unsigned long)size;
    }
    return ptr;
}

unsigned long CodePage_EmbeddedCount(void) {
    return (unsigned long)GMBXWC_TABLE_N;
}

BOOL CodePage_EmbeddedInfo(unsigned long index, EmbeddedTableInfo* p_info) {
    if (p_info == NULL) {
        return FALSE;
    }
    if (index >= GMBXWC_TABLE_N) {
        return FALSE;
    }
    p_info->code_page = g_tables[index].code_page;
    p_info->blob_name = g_tables[index].blob_name;
    p_info->display_name = g_tables[index].display_name;
    return TRUE;
}

BOOL CodePage_FindIndexForCodePage(unsigned long code_page, unsigned long* p_index) {
    unsigned long i;
    for (i = 0; i < (unsigned long)GMBXWC_TABLE_N; i++) {
        if (g_tables[i].code_page == code_page) {
            if (p_index) {
                *p_index = i;
            }
            return TRUE;
        }
    }
    return FALSE;
}

CodePageContext* CodePage_CreateConverter(unsigned long index) {
    const unsigned char* blob;
    blob = LockedBlobByIndex(index, NULL);
    if (blob == NULL) {
        return NULL;
    }
    return InitCodePageConverter(blob);
}

unsigned long CodePage_IndexedMB2WC(unsigned long index,
                      const unsigned char* src, unsigned long src_len,
                      wchar_t* dest, unsigned long dest_max, BOOL* lpbUnmapped) {
    CodePageContext* ctx;
    unsigned long result;
    ctx = CodePage_CreateConverter(index);
    if (ctx == NULL) {
        return 0;
    }
    result = CodePage_MB2WC(ctx, src, src_len, dest, dest_max, lpbUnmapped);
    FreeCodePageConverter(ctx);
    return result;
}

unsigned long CodePage_IndexedWC2MB(unsigned long index,
                      const wchar_t* src, unsigned long src_len,
                      unsigned char* dest, unsigned long dest_max, BOOL* lpbUnmapped) {
    CodePageContext* ctx;
    unsigned long result;
    ctx = CodePage_CreateConverter(index);
    if (ctx == NULL) {
        return 0;
    }
    result = CodePage_WC2MB(ctx, src, src_len, dest, dest_max, lpbUnmapped);
    FreeCodePageConverter(ctx);
    return result;
}
