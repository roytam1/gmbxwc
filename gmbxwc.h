/* Unified Conversion Functions */
size_t CodePage_MB2WC(const CodePageContext* ctx, 
                      const unsigned char* src, size_t src_len, 
                      wchar_t* dest, size_t dest_max);

size_t CodePage_WC2MB(const CodePageContext* ctx, 
                      const wchar_t* src, size_t src_len, 
                      unsigned char* dest, size_t dest_max);

#if 0
/* sample code for using library */
void RenderFilename(const CodePageContext* ctx, const unsigned char* raw_mb_str, size_t len) {
    wchar_t* w_buffer;
    size_t required_chars;

    /* Step 1: Query necessary character allocation length */
    required_chars = CodePage_MB2WC(ctx, raw_mb_str, len, NULL, 0);
    
    if (required_chars == 0) return;

    /* Step 2: Allocate safely on local heap (Win32 traditional style) */
    w_buffer = (wchar_t*)LocalAlloc(LMEM_FIXED, (required_chars + 1) * sizeof(wchar_t));
    if (!w_buffer) return;

    /* Step 3: Run safe actual transformation */
    CodePage_MB2WC(ctx, raw_mb_str, len, w_buffer, required_chars);
    w_buffer[required_chars] = L'\0'; /* Force null-terminator */

    /* Step 4: Draw to Win32 GDI Canvas */
    TextOutW(hdc, 10, 10, w_buffer, required_chars);

    LocalFree(w_buffer);
}

#include "cp932_embedded.h" /* Contains embedded 'cp932_blob_data' array */

/* Application Global or Subsystem State variables */
CodePageContext g_CtxShiftJIS;   /* Embedded Engine Context */
CodePageContext g_CtxEBCDIC;     /* External Engine Context */
unsigned char* g_EbcdicBlob = NULL; /* Alloc tracking pointer for external data */

/* Call this during your Image Viewer initialization phase */
void InitApplicationEncodings(void) {
    /* 1. INITIALIZE EMBEDDED TABLES */
    /* Point straight to the array compiled directly into the binary image */
    g_CtxShiftJIS = InitCodePageConverter(cp932_blob_data);

    /* 2. INITIALIZE EXTERNAL TABLES */
    /* Attempt to pull heavy data from a sidecar file next to the EXE */
    g_EbcdicBlob = LoadExternalCodePageBlob("CP933.DAT");
    if (g_EbcdicBlob != NULL) {
        g_CtxEBCDIC = InitCodePageConverter(g_EbcdicBlob);
    } else {
        /* Handle graceful fallback or mark the context invalid */
        g_CtxEBCDIC.is_valid = 0;
    }
}

/* Call this when the image viewer window is closed or exiting */
void CleanupApplicationEncodings(void) {
    /* Embedded contexts require zero teardown (they live in read-only program space) */
    
    /* External tables must explicitly release their allocated heap footprint */
    if (g_EbcdicBlob != NULL) {
        FreeExternalCodePageBlob(g_EbcdicBlob);
        g_EbcdicBlob = NULL;
    }
}
#endif