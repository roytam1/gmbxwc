/* gmbxwc_import.h - consumer header for implicit linking with gmbxwc.dll.
 *
 * Include THIS file (instead of gmbxwc.h) in programs that call the DLL
 * through its import library, and link libgmbxwc.a (MinGW) or the MSVC
 * import library built from gmbxwc.def:
 *
 *   gcc -o myapp.exe myapp.c -Lgmbxwc-dir -lgmbxwc
 *
 * For explicit run-time linking (LoadLibrary + GetProcAddress) use
 * gmbxwc_dynload.h instead.
 */
#ifndef _GMBXWC_IMPORT_H_
#define _GMBXWC_IMPORT_H_

#if !defined(GMBXWC_USE_DLL) && !defined(GMBXWC_BUILD_DLL)
#define GMBXWC_USE_DLL 1
#endif

#include "gmbxwc.h"

#endif /* _GMBXWC_IMPORT_H_ */
