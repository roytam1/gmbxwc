#include "gmbxwc.h"

/* Internal Helper: Binary Search for Extension B */
static unsigned short FindExtB(const ExtBMapping* table, int count, unsigned long codepoint) {
    int left = 0;
    int right = count - 1;
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (table[mid].codepoint == codepoint) return table[mid].dbcs_value;
        if (table[mid].codepoint < codepoint) left = mid + 1;
        else right = mid - 1;
    }
    return 0;
}

CodePageContext InitCodePageConverter(const unsigned char* blob_data) {
    CodePageContext ctx;
    const CodePageHeader* h = (const CodePageHeader*)blob_data;

    ctx.is_valid = 0;
    ctx.magic = h->magic;
    ctx.code_page = h->code_page;
    ctx.is_stateful_ebcdic = 0;
    ctx.is_gb18030 = 0;
    ctx.is_32bit_pool = h->is_32bit_pool;
    
    ctx.trail_windows = (const ResourceTrailWindow*)(blob_data + h->off_windows);
    ctx.pool16 = (const unsigned short*)(blob_data + h->off_pool);
    ctx.pool32 = (const unsigned long*)(blob_data + h->off_pool);
    ctx.wchar_directory = (const unsigned short*)(blob_data + h->off_dir);
    ctx.wchar_dir_count = h->wchar_dir_count; /* NEW: Stored safely in context */
    ctx.wchar_page_pool = (const unsigned short*)(blob_data + h->off_pages);
    
    ctx.ext_b_table = 0;
    ctx.ext_b_count = 0;
    ctx.gb_ranges = 0;
    ctx.gb_range_count = 0;
    ctx.dbcs_lead_table = 0;
    ctx.sbcs_table = 0;
    ctx.dbcs_first_byte_table = 0;

    /* Route 1: GB18030 Engine Configuration */
    if (h->magic == 0x38314247) { /* 'GB18' */
        ctx.is_gb18030 = 1;
        ctx.dbcs_lead_table = (const unsigned short*)(blob_data + sizeof(CodePageHeader));
        ctx.gb_ranges = (const GB18030Range*)(blob_data + h->off_extra);
        ctx.gb_range_count = h->extra_count;
        ctx.is_valid = 1;
    }
    /* Route 2: Stateful EBCDIC Configuration */
    else if (h->magic == 0x39313243) { /* 'C219' */
        ctx.is_stateful_ebcdic = 1;
        ctx.sbcs_table = (const unsigned short*)(blob_data + sizeof(CodePageHeader));
        ctx.dbcs_first_byte_table = (const unsigned short*)(blob_data + sizeof(CodePageHeader) + 512);
        ctx.is_valid = 1;
    }
    /* Route 3: Standard Stateless DBCS Configuration */
    else if (h->magic == 0x4C425043) { /* 'CPBL' */
        ctx.dbcs_lead_table = (const unsigned short*)(blob_data + sizeof(CodePageHeader));
        if (h->off_extra != 0) {
            ctx.ext_b_table = (const ExtBMapping*)(blob_data + h->off_extra);
            ctx.ext_b_count = h->extra_count;
        }
        ctx.is_valid = 1;
    }

    return ctx;
}

/* ========================================================================= */
/* MULTIBYTE -> WIDECHAR UNIFIED IMPLEMENTATION                              */
/* ========================================================================= */
unsigned long CodePage_MB2WC(const CodePageContext* ctx, const unsigned char* src, unsigned long src_len, wchar_t* dest, unsigned long dest_max, BOOL* lpbUnmapped) {
    const unsigned char* src_end;
    unsigned long written = 0;
    int ebcdic_mode = EBCDIC_MODE_SBCS;

    if (!ctx || !ctx->is_valid || !src) return 0;
    src_end = src + src_len;

    while (src < src_end) {
        unsigned char b1 = *src++;
        unsigned long cp_val = 0;

        /* GB18030 Stream Processing Path */
        if (ctx->is_gb18030) {
            unsigned short lead_info = ctx->dbcs_lead_table[b1];
            if ((lead_info & 0x8000) == 0) {
                cp_val = lead_info; /* 1-Byte ASCII Match */
            } else {
                if (src >= src_end) {
                    if (lpbUnmapped) *lpbUnmapped = TRUE;
                    break;
                }
                {
                    unsigned char b2 = *src;
                    /* Check if the second byte marks an algorithmic 4-byte boundary */
                    if (b2 >= 0x30 && b2 <= 0x39) {
                        src++; /* Safe to consume b2 */
                        if (src + 2 > src_end) {
                            if (lpbUnmapped) *lpbUnmapped = TRUE;
                            break;
                        }
                        {
                            unsigned char b3 = *src++;
                            unsigned char b4 = *src++;
                            unsigned long idx = (b1 - 0x81) * 12600UL + (b2 - 0x30) * 1260UL + (b3 - 0x81) * 10UL + (b4 - 0x30);
                            long left = 0;
                            long right = (long)ctx->gb_range_count - 1;
                            int range_found = 0;

                            cp_val = 0xFFFD;
                            while (left <= right) {
                                long mid = left + (right - left) / 2;
                                const GB18030Range* r = &ctx->gb_ranges[mid];
                                if (idx >= r->start_index && idx <= r->end_index) {
                                    cp_val = r->start_unicode + (idx - r->start_index);
                                    range_found = 1;
                                    break;
                                }
                                if (r->start_index < idx) left = mid + 1;
                                else right = mid - 1;
                            }
                            if (!range_found) {
                                if (lpbUnmapped) *lpbUnmapped = TRUE;
                            }
                        }
                    } else {
                        /* Normal 2-Byte Range execution pass */
                        unsigned char b2_real = *src++;
                        unsigned short w_idx = lead_info & 0x7FFF;
                        ResourceTrailWindow w = ctx->trail_windows[w_idx];
                        if (b2_real >= w.min_trail && b2_real <= w.max_trail) {
                            cp_val = ctx->pool16[w.pool_offset + (b2_real - w.min_trail)];
                            if (cp_val == 0 || cp_val == 0xFFFD) {
                                cp_val = 0xFFFD;
                                if (lpbUnmapped) *lpbUnmapped = TRUE;
                            }
                        } else {
                            cp_val = 0xFFFD;
                            if (lpbUnmapped) *lpbUnmapped = TRUE;
                        }
                    }
                }
            }
        }
        /* Path A: Stateful EBCDIC Logic */
        else if (ctx->is_stateful_ebcdic) {
            if (b1 == 0x0E) { ebcdic_mode = EBCDIC_MODE_DBCS; continue; }
            if (b1 == 0x0F) { ebcdic_mode = EBCDIC_MODE_SBCS; continue; }

            if (ebcdic_mode == EBCDIC_MODE_SBCS) {
                cp_val = ctx->sbcs_table[b1];
                if (cp_val == 0 || cp_val == 0xFFFD) {
                    cp_val = 0xFFFD;
                    if (lpbUnmapped) *lpbUnmapped = TRUE;
                }
            } else {
                if (src >= src_end) {
                    if (lpbUnmapped) *lpbUnmapped = TRUE;
                    break;
                }
                {
                    unsigned char b2 = *src++;
                    unsigned short w_idx = ctx->dbcs_first_byte_table[b1];
                    if (w_idx != 0xFFFF) {
                        ResourceTrailWindow w = ctx->trail_windows[w_idx];
                        if (b2 >= w.min_trail && b2 <= w.max_trail) {
                            cp_val = ctx->pool16[w.pool_offset + (b2 - w.min_trail)];
                            if (cp_val == 0 || cp_val == 0xFFFD) {
                                cp_val = 0xFFFD;
                                if (lpbUnmapped) *lpbUnmapped = TRUE;
                            }
                        } else {
                            cp_val = 0xFFFD;
                            if (lpbUnmapped) *lpbUnmapped = TRUE;
                        }
                    } else {
                        cp_val = 0xFFFD;
                        if (lpbUnmapped) *lpbUnmapped = TRUE;
                    }
                }
            }
        } 
        /* Path B: Stateless DBCS Logic (Big5, Shift-JIS, etc) */
        else {
            unsigned short lead_info = ctx->dbcs_lead_table[b1];
            if ((lead_info & 0x8000) == 0) {
                cp_val = lead_info;
                if (cp_val == 0xFFFD || (cp_val == 0 && b1 != 0)) {
                    if (lpbUnmapped) *lpbUnmapped = TRUE;
                }
            } else {
                if (src >= src_end) {
                    if (lpbUnmapped) *lpbUnmapped = TRUE;
                    break;
                }
                {
                    unsigned char b2_real = *src++;
                    unsigned short w_idx = lead_info & 0x7FFF;
                    ResourceTrailWindow w = ctx->trail_windows[w_idx];

                    if (b2_real >= w.min_trail && b2_real <= w.max_trail) {
                        if (ctx->is_32bit_pool) {
                            cp_val = ctx->pool32[w.pool_offset + (b2_real - w.min_trail)];
                        } else {
                            cp_val = ctx->pool16[w.pool_offset + (b2_real - w.min_trail)];
                        }
                        if (cp_val == 0 || cp_val == 0xFFFD) {
                            cp_val = 0xFFFD;
                            if (lpbUnmapped) *lpbUnmapped = TRUE;
                        }
                    } else {
                        cp_val = 0xFFFD;
                        if (lpbUnmapped) *lpbUnmapped = TRUE;
                    }
                }
            }
        }

        /* Write value to destination buffer */
        if (cp_val > 0xFFFF) {
            if (dest) {
                if (written + 2 > dest_max) break;
                cp_val -= 0x10000;
                dest[written++] = (wchar_t)(0xD800 + (cp_val >> 10));
                dest[written++] = (wchar_t)(0xDC00 + (cp_val & 0x3FF));
            } else { written += 2; }
        } else {
            if (dest) {
                if (written + 1 > dest_max) break;
                dest[written++] = (wchar_t)cp_val;
            } else { written += 1; }
        }
    }
    return written;
}

/* ========================================================================= */
/* WIDECHAR -> MULTIBYTE UNIFIED IMPLEMENTATION                              */
/* ========================================================================= */
unsigned long CodePage_WC2MB(const CodePageContext* ctx, const wchar_t* src, unsigned long src_len, unsigned char* dest, unsigned long dest_max, BOOL* lpbUnmapped) {
    const wchar_t* src_end;
    unsigned long written = 0;
    int ebcdic_mode = EBCDIC_MODE_SBCS;

    if (!ctx || !ctx->is_valid || !src) return 0;
    src_end = src + src_len;

    while (src < src_end) {
        wchar_t wc = *src++;
        unsigned long cp_val = 0;

        /* Resolve UTF-16 Surrogate Pairs to a flat 32-bit Unicode Codepoint */
        if (wc >= 0xD800 && wc <= 0xDBFF) {
            if (src >= src_end) {
                if (lpbUnmapped) *lpbUnmapped = TRUE;
                break;
            }
            {
                wchar_t low = *src++;
                cp_val = 0x10000 + ((wc - 0xD800) << 10) + (low - 0xDC00);
            }
        } else {
            cp_val = wc;
        }

        /* GB18030 Encoding Logic Injection Path */
        if (ctx->is_gb18030) {
            unsigned short page_idx = 0;
            unsigned long trie_dbcs = 0;
            unsigned long page_num = cp_val >> 8;

            /* Guard directory access against structural table bounds */
            if (page_num < ctx->wchar_dir_count) {
                page_idx = ctx->wchar_directory[page_num];
                if (page_idx != 0xFFFF) {
                    trie_dbcs = ctx->wchar_page_pool[page_idx * 256 + (cp_val & 0xFF)];
                }
            }

            if (trie_dbcs != 0) {
                if (trie_dbcs <= 0xFF) {
                    if (dest) { if (written + 1 > dest_max) break; dest[written++] = (unsigned char)trie_dbcs; } else { written++; }
                } else {
                    if (dest) {
                        if (written + 2 > dest_max) break;
                        dest[written++] = (unsigned char)(trie_dbcs >> 8);
                        dest[written++] = (unsigned char)(trie_dbcs & 0xFF);
                    } else { written += 2; }
                }
            } else {
                /* Trie miss indicates target falls into 4-byte algorithmic space */
                long left = 0;
                long right = (long)ctx->gb_range_count - 1;
                int found = 0;
                while (left <= right) {
                    long mid = left + (right - left) / 2;
                    const GB18030Range* r = &ctx->gb_ranges[mid];
                    unsigned long r_len = r->end_index - r->start_index;
                    if (cp_val >= r->start_unicode && cp_val <= (r->start_unicode + r_len)) {
                        unsigned char b4, b3, b2, b1;
                        unsigned long idx = r->start_index + (cp_val - r->start_unicode);
                        b4 = (unsigned char)(0x30 + (idx % 10)); idx /= 10;
                        b3 = (unsigned char)(0x81 + (idx % 126)); idx /= 126;
                        b2 = (unsigned char)(0x30 + (idx % 10)); idx /= 10;
                        b1 = (unsigned char)(0x81 + idx);
                        
                        if (dest) {
                            if (written + 4 > dest_max) break;
                            dest[written++] = b1; dest[written++] = b2;
                            dest[written++] = b3; dest[written++] = b4;
                        } else { written += 4; }
                        found = 1; break;
                    }
                    if (r->start_unicode < cp_val) left = mid + 1;
                    else right = mid - 1;
                }
                if (!found) {
                    if (lpbUnmapped) *lpbUnmapped = TRUE;
                    if (dest) { if (written + 1 > dest_max) break; dest[written++] = '?'; } else { written++; }
                }
            }
            continue;
        }

        /* Native MultiByte Lookups (Supports BMP + Big5-HKSCS Plane 2 Mappings) */
        {
            unsigned long target_mb = 0;
            BOOL is_unmapped_char = FALSE;
            unsigned long page_num = cp_val >> 8;

            /* Ensure the resolved page fits within the loaded code-page directory bounds */
            if (page_num < ctx->wchar_dir_count) {
                unsigned short page_idx = ctx->wchar_directory[page_num];

                if (page_idx != 0xFFFF) {
                    target_mb = ctx->wchar_page_pool[page_idx * 256 + (cp_val & 0xFF)];

                    /* Table yields 0 on a non-null input -> unmapped sequence */
                    if (target_mb == 0 && cp_val != 0) {
                        is_unmapped_char = TRUE;
                    }
                } else {
                    is_unmapped_char = TRUE; /* Entire 256-char page is missing from this code page */
                }
            } else {
                is_unmapped_char = TRUE; /* Out of physical Unicode range limits for this code page */
            }

            if (is_unmapped_char) {
                if (lpbUnmapped) *lpbUnmapped = TRUE;
                target_mb = 0x3F; /* '?' fallback */
            }

            /* State-dependent structural serialization step */
            if (ctx->is_stateful_ebcdic) {
                if (target_mb <= 0xFF) {
                    if (ebcdic_mode == EBCDIC_MODE_DBCS) {
                        if (dest) { if (written + 1 > dest_max) break; dest[written++] = 0x0F; } else { written++; }
                        ebcdic_mode = EBCDIC_MODE_SBCS;
                    }
                    if (dest) { if (written + 1 > dest_max) break; dest[written++] = (unsigned char)target_mb; } else { written++; }
                } else {
                    if (ebcdic_mode == EBCDIC_MODE_SBCS) {
                        if (dest) { if (written + 1 > dest_max) break; dest[written++] = 0x0E; } else { written++; }
                        ebcdic_mode = EBCDIC_MODE_DBCS;
                    }
                    if (dest) {
                        if (written + 2 > dest_max) break;
                        dest[written++] = (unsigned char)(target_mb >> 8);
                        dest[written++] = (unsigned char)(target_mb & 0xFF);
                    } else { written += 2; }
                }
            } else {
                /* Standard Stateless MultiByte Writer (Shift-JIS, Big5-HKSCS, etc.) */
                if (target_mb <= 0xFF) {
                    if (dest) { if (written + 1 > dest_max) break; dest[written++] = (unsigned char)target_mb; } else { written++; }
                } else {
                    if (dest) {
                        if (written + 2 > dest_max) break;
                        dest[written++] = (unsigned char)(target_mb >> 8);
                        dest[written++] = (unsigned char)(target_mb & 0xFF);
                    } else { written += 2; }
                }
            }
        }
    }
    return written;
}
