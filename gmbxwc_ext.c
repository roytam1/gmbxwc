/* gmbxwc_ext.c - gmbxwc_ext.dll external-bundle glue (C89).
 *
 * Same dense 0-based index API as gmbxwc_dll.c (declared in gmbxwc.h),
 * but the CPBL blobs AND the inventory come from a single external
 * bundle file instead of Windows resources. The bundle (default
 * gmbxwc.dat, built with build_cpbl_bundle.py from the g_tables[]
 * inventory in gmbxwc_dll.c) is auto-loaded on first use: first from the
 * directory holding this DLL, then from the current directory.
 *
 * Bundle layout (all integers little-endian):
 *   offset 0 : magic 4s 'CPBX', version u32 (=1), count u32
 *   offset 12: count directory entries, 108 bytes each:
 *              code_page u32, blob_offset u32 (from file start),
 *              blob_size u32, blob_name char[32] (NUL-terminated),
 *              display_name char[64] (NUL-terminated, UTF-8)
 *   then the raw CPBL blobs back to back.
 *
 * This file keeps NO compiled-in table. If the bundle is missing or fails
 * validation, CodePage_EmbeddedCount returns 0 and every other entry point
 * behaves as if the index were out of range (Info/FindIndex FALSE,
 * CreateConverter NULL, one-shot wrappers 0).
 *
 * The whole bundle is read into memory once and kept until unload;
 * contexts and EmbeddedTableInfo strings point into it, so free every ctx
 * with FreeCodePageConverter (and stop using info strings) BEFORE
 * unloading the DLL.
 *
 * NEVER link this file together with gmbxwc_dll.c: both define DllMain
 * and the same API entry points. gmbxwc_ext.dll = gmbxwc.c + this file.
 */
#include "gmbxwc.h"
#include <stdio.h>
#include <string.h>

#define GMBXWC_BUNDLE_NAME "gmbxwc.dat"
#define GMBXWC_BUNDLE_MAGIC 0x58425043UL /* 'CPBX' */
#define GMBXWC_BUNDLE_VERSION 1UL
#define GMBXWC_BUNDLE_MAX_TABLES 256UL
#define GMBXWC_BUNDLE_NAME_LEN 32
#define GMBXWC_BUNDLE_DISP_LEN 64
#define GMBXWC_BUNDLE_DIR_REC (12 + GMBXWC_BUNDLE_NAME_LEN + GMBXWC_BUNDLE_DISP_LEN)
#define GMBXWC_BUNDLE_HDR_LEN 12
#define GMBXWC_BUNDLE_OFF_CP 0
#define GMBXWC_BUNDLE_OFF_BLOB_OFF 4
#define GMBXWC_BUNDLE_OFF_BLOB_SIZE 8
#define GMBXWC_BUNDLE_OFF_NAME 12
#define GMBXWC_BUNDLE_OFF_DISP (12 + GMBXWC_BUNDLE_NAME_LEN)

static HINSTANCE g_hExtInstance = NULL;
static CRITICAL_SECTION g_cs;
static int g_cs_ready = 0;
static unsigned char* g_bundle = NULL;
static unsigned long g_bundle_len = 0;
static unsigned long g_table_count = 0;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hExtInstance = hinstDLL;
        InitializeCriticalSection(&g_cs);
        g_cs_ready = 1;
        /*DisableThreadLibraryCalls(hinstDLL);*/
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (g_bundle != NULL) {
            free(g_bundle);
            g_bundle = NULL;
            g_bundle_len = 0;
            g_table_count = 0;
        }
        if (g_cs_ready) {
            DeleteCriticalSection(&g_cs);
            g_cs_ready = 0;
        }
    }
    return TRUE;
}

static unsigned long ReadU32LE(const unsigned char* p) {
    return ((unsigned long)p[0])
         | ((unsigned long)p[1] << 8)
         | ((unsigned long)p[2] << 16)
         | ((unsigned long)p[3] << 24);
}

/* out[] = directory of this DLL + "\" + bundle name; "" on failure. */
static void BuildBundlePath(char* out, unsigned long out_len) {
    DWORD len;
    DWORD i;
    unsigned long dir_len;
    unsigned long name_len;
    out[0] = '\0';
    if (g_hExtInstance == NULL || out_len == 0) {
        return;
    }
    len = GetModuleFileNameA(g_hExtInstance, out, MAX_PATH);
    if (len == 0 || len >= (DWORD)MAX_PATH) {
        out[0] = '\0';
        return;
    }
    out[len] = '\0';
    i = len;
    while (i > 0 && out[i - 1] != '\\' && out[i - 1] != '/' && out[i - 1] != ':') {
        i--;
    }
    dir_len = (unsigned long)i;
    name_len = (unsigned long)strlen(GMBXWC_BUNDLE_NAME);
    if (dir_len + name_len + 1 > out_len) {
        out[0] = '\0';
        return;
    }
    memcpy(out + dir_len, GMBXWC_BUNDLE_NAME, name_len + 1);
}

/* Caller must hold g_cs (or run lock-free pre-DllMain for static links).
   Returns 1 with g_bundle/g_table_count published, 0 on any failure
   (nothing stored). */
static int TryLoadBundle(void) {
    char path[MAX_PATH];
    FILE* fp;
    long fsize;
    unsigned long file_len;
    unsigned char* buf;
    unsigned long count;
    unsigned long dir_end;
    unsigned long i;
    const unsigned char* e;
    unsigned long cp;
    unsigned long off;
    unsigned long sz;
    const char* nm;
    const char* dsp;
    const CodePageHeader* h;
    if (g_bundle != NULL) {
        return 1;
    }
    BuildBundlePath(path, MAX_PATH);
    fp = NULL;
    if (path[0] != '\0') {
        fp = fopen(path, "rb");
    }
    if (fp == NULL) {
        fp = fopen(GMBXWC_BUNDLE_NAME, "rb"); /* current-directory fallback */
    }
    if (fp == NULL) {
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize < (long)GMBXWC_BUNDLE_HDR_LEN) {
        fclose(fp);
        return 0;
    }
    file_len = (unsigned long)fsize;
    buf = (unsigned char*)malloc(file_len);
    if (buf == NULL) {
        fclose(fp);
        return 0;
    }
    if (fread(buf, 1, file_len, fp) != file_len) {
        free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    if (ReadU32LE(buf) != GMBXWC_BUNDLE_MAGIC
        || ReadU32LE(buf + 4) != GMBXWC_BUNDLE_VERSION) {
        free(buf);
        return 0;
    }
    count = ReadU32LE(buf + 8);
    if (count == 0 || count > GMBXWC_BUNDLE_MAX_TABLES) {
        free(buf);
        return 0;
    }
    dir_end = GMBXWC_BUNDLE_HDR_LEN + count * GMBXWC_BUNDLE_DIR_REC;
    if (dir_end > file_len) {
        free(buf);
        return 0;
    }
    for (i = 0; i < count; i++) {
        e = buf + GMBXWC_BUNDLE_HDR_LEN + i * GMBXWC_BUNDLE_DIR_REC;
        cp = ReadU32LE(e + GMBXWC_BUNDLE_OFF_CP);
        off = ReadU32LE(e + GMBXWC_BUNDLE_OFF_BLOB_OFF);
        sz = ReadU32LE(e + GMBXWC_BUNDLE_OFF_BLOB_SIZE);
        nm = (const char*)(e + GMBXWC_BUNDLE_OFF_NAME);
        dsp = (const char*)(e + GMBXWC_BUNDLE_OFF_DISP);
        if (memchr(nm, '\0', GMBXWC_BUNDLE_NAME_LEN) == NULL) {
            free(buf);
            return 0;
        }
        if (memchr(dsp, '\0', GMBXWC_BUNDLE_DISP_LEN) == NULL) {
            free(buf);
            return 0;
        }
        if (sz < sizeof(CodePageHeader) || off > file_len || sz > file_len - off) {
            free(buf);
            return 0;
        }
        h = (const CodePageHeader*)(buf + off);
        if (h->magic != 0x4C425043UL && h->magic != 0x38314247UL && h->magic != 0x39313243UL) {
            free(buf);
            return 0;
        }
        if (h->code_page != cp) {
            free(buf);
            return 0;
        }
    }
    g_bundle = buf;
    g_bundle_len = file_len;
    g_table_count = count;
    return 1;
}

static int EnsureBundleLoaded(void) {
    int ok;
    if (g_bundle != NULL) {
        return 1;
    }
    if (!g_cs_ready) {
        return TryLoadBundle(); /* static-link edge: no DllMain, no lock */
    }
    EnterCriticalSection(&g_cs);
    ok = TryLoadBundle();
    LeaveCriticalSection(&g_cs);
    return ok;
}

/* Directory record for a dense index, or NULL (bad index / no bundle). */
static const unsigned char* BundleDirByIndex(unsigned long index) {
    if (!EnsureBundleLoaded()) {
        return NULL;
    }
    if (g_bundle == NULL || index >= g_table_count) {
        return NULL;
    }
    return g_bundle + GMBXWC_BUNDLE_HDR_LEN + index * GMBXWC_BUNDLE_DIR_REC;
}

/* Blob bytes for a dense index, or NULL (bad index / bundle unavailable). */
static const unsigned char* BundleBlobByIndex(unsigned long index) {
    const unsigned char* e;
    unsigned long off;
    e = BundleDirByIndex(index);
    if (e == NULL) {
        return NULL;
    }
    off = ReadU32LE(e + GMBXWC_BUNDLE_OFF_BLOB_OFF);
    return g_bundle + off;
}

unsigned long CodePage_EmbeddedCount(void) {
    if (!EnsureBundleLoaded()) {
        return 0;
    }
    return g_table_count;
}

BOOL CodePage_EmbeddedInfo(unsigned long index, EmbeddedTableInfo* p_info) {
    const unsigned char* e;
    if (p_info == NULL) {
        return FALSE;
    }
    e = BundleDirByIndex(index);
    if (e == NULL) {
        return FALSE;
    }
    p_info->code_page = ReadU32LE(e + GMBXWC_BUNDLE_OFF_CP);
    p_info->blob_name = (const char*)(e + GMBXWC_BUNDLE_OFF_NAME);
    p_info->display_name = (const char*)(e + GMBXWC_BUNDLE_OFF_DISP);
    return TRUE;
}

BOOL CodePage_FindIndexForCodePage(unsigned long code_page, unsigned long* p_index) {
    unsigned long i;
    const unsigned char* e;
    if (!EnsureBundleLoaded()) {
        return FALSE;
    }
    for (i = 0; i < g_table_count; i++) {
        e = g_bundle + GMBXWC_BUNDLE_HDR_LEN + i * GMBXWC_BUNDLE_DIR_REC;
        if (ReadU32LE(e + GMBXWC_BUNDLE_OFF_CP) == code_page) {
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
    blob = BundleBlobByIndex(index);
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
