#include <stdio.h>
#include "gmbxwc.h"


int main(int argc, char** argv) {
	CodePageContext* cnvCtx;
	unsigned char* cnvCPBL;
	unsigned char* InData;
	wchar_t* OutData;
	FILE* fp;
	unsigned long fsize, frsize, outchars;
	BOOL unmap_trapped = 0;

	if(argc < 4) {
		printf("%s <CPBL-data-file> <input-filename> <output-filename>\n", argv[0]);
		return 1;
	}

	/* read and init CPBL context */
	fp = fopen(argv[1], "rb");
	fseek(fp, 0, SEEK_END);
	fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	cnvCPBL = (unsigned char*)malloc(fsize);
	if(!cnvCPBL) {
		printf("unable to allocate memory for CPBL\n");
		return 1;
	}
	frsize = fread(cnvCPBL, 1, fsize, fp);
	fclose(fp);
	if(frsize!=fsize) {
		printf("unable to read CPBL (%d vs %d)\n",fsize,frsize);
		return 1;
	}

	cnvCtx = InitCodePageConverter(cnvCPBL);
	if(cnvCtx->is_valid != 1) {
		printf("unable to init CPBL context\n");
		return 1;
	}

	/* read source file */
	fp = fopen(argv[2], "rb");
	fseek(fp, 0, SEEK_END);
	fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	InData = (unsigned char*)malloc(fsize);
	if(!InData) {
		printf("unable to allocate memory for input file\n");
		return 2;
	}

	frsize = fread(InData, 1, fsize, fp);
	fclose(fp);
	if(frsize!=fsize) {
		printf("unable to read input file\n");
		return 2;
	}

	/* output is in widechar size */
	frsize = CodePage_MB2WC(cnvCtx, InData, fsize, 0, 0, &unmap_trapped);
	if(!frsize) {
		printf("Converter failed to convert input data\n");
		return 3;
	}

	/* allocate output size in bytes */
	frsize=frsize * sizeof(wchar_t);
	printf("estimating input data (size=%d) to output size=%d...\n", fsize, frsize);
	OutData = (wchar_t*)malloc(frsize);
	if(!OutData) {
		printf("unable to allocate memory for output file\n");
		return 4;
	}

	/* really converting to wide char */
	outchars = CodePage_MB2WC(cnvCtx, InData, fsize, OutData, frsize, &unmap_trapped);
	if(!outchars) {
		printf("Converter failed to convert input data\n");
		return 3;
	}
	if(unmap_trapped) {
		printf("WARNING: some multibyte data has no mapping to proper unicode codepoint\n");
	}

	fp = fopen(argv[3], "wb");
	/* write BOM first */
	fprintf(fp, "\xff\xfe");
	fwrite(OutData, sizeof(wchar_t), outchars, fp);
	fclose(fp);

	/* free all memory */
	free(OutData);
	free(InData);
	FreeCodePageConverter(cnvCtx);
	free(cnvCPBL);
	return 0;
}
