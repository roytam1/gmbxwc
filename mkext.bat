rem Build gmbxwc_ext.dll (external-bundle backend). Run from this directory.
rem gmbxwc_ext.c comes FIRST so the output is named gmbxwc_ext.dll.
rem Needs gmbxwc.dat (python build_cpbl_bundle.py) next to the DLL at run time.
cl /Ox /DGMBXWC_BUILD_DLL /LD gmbxwc_ext.c gmbxwc.c %1 %2 %3 %4 %5 %6 %7 %8 %9
