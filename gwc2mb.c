#include <stdio.h>
#include "gmbxwc.h"


int main(int argc, char** argv) {
	CodePageContext* cnvCtx;
	unsigned char *cnvCPBL;
	unsigned char *InData, *InDataPointer;
	unsigned char *OutData;
	FILE *fp;
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

	InDataPointer = InData = (unsigned char*)malloc(fsize);
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

	/* skip BOM */
	if(fsize > 2 && *InData==0xff && *(InData+1)==0xfe)
		InData += 2, fsize -= 2;

	/* assume input data is UTF-16LE, total char count is half of file size */
	fsize /= 2;

	/* output is in widechar size */
	frsize = CodePage_WC2MB(cnvCtx, (wchar_t*)InData, fsize, 0, 0, &unmap_trapped);
	if(!frsize) {
		printf("Converter failed to convert input data\n");
		return 3;
	}

	/* allocate output size in bytes */
	frsize=frsize * sizeof(unsigned char);
	printf("estimating input data (size=%d) to output size=%d...\n", fsize, frsize);
	OutData = (unsigned char*)malloc(frsize);
	if(!OutData) {
		printf("unable to allocate memory for output file\n");
		return 4;
	}

	/* really converting to wide char */
	outchars = CodePage_WC2MB(cnvCtx, (wchar_t*)InData, fsize, OutData, frsize, &unmap_trapped);
	if(!outchars) {
		printf("Converter failed to convert input data\n");
		return 3;
	}
	if(unmap_trapped) {
		printf("WARNING: some unicode codepoints has no mapping to proper multibyte\n");
	}

	fp = fopen(argv[3], "wb");
	fwrite(OutData, sizeof(unsigned char), outchars, fp);
	fclose(fp);

	/* free all memory */
	free(OutData);
	free(InDataPointer);
	FreeCodePageConverter(cnvCtx);
	free(cnvCPBL);
	return 0;
}
