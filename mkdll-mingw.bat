rem Build gmbxwc.dll (MinGW). Run from this directory.
rem Requires C:\msys64\mingw64\bin on PATH for windres + gcc.
windres gmbxwc.rc -o gmbxwc_res.o
gcc -DGMBXWC_BUILD_DLL -shared -o gmbxwc.dll gmbxwc.c gmbxwc_dll.c gmbxwc_res.o -Wl,--out-implib,libgmbxwc.a
