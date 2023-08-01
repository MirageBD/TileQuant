#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "bitmap.h"
#include "colourspace.h"
#include "qualetize.h"
#include "tiles.h"

#define MEASURE_PSNR 1

#define ARGMATCH(Input, Target) \
	ArgStr = Input + strlen(Target); \
	if(!memcmp(Input, Target, strlen(Target)))

#define DITHERMODE_MATCH(Input, Target, ModeValue, DefaultLevel) \
	d = mystrcmp(Input, Target); \
	if(!d || d == ',') { \
		ArgOk = 1; \
		DitherMode  = ModeValue; \
		DitherLevel = !d ? DefaultLevel : atof(strchr(Input, ',')+1); \
	}

static int mystrcmp(const char *s1, const char *s2)
{
	while(*s1 && *s1 == *s2) s1++, s2++;
	return *s1 - *s2;
}

int main(int argc, const char *argv[])
{
	if(argc < 3)
	{
		printf(
			"\n"
			"Usage:\n"
			"    tilequant Input.bmp Output.bmp [options]\n"
			"Options:\n"
			"    -np:16            - Set number of palettes available\n"
			"    -ps:16            - Set number of colours per palette\n"
			"    -tw:8             - Set tile width\n"
			"    -th:8             - Set tile height\n"
			"    -bgra:5551        - Set BGRA bit depth\n"
			"    -dither:floyd,1.0 - Set dither mode, level\n"
			"    -order            - Order colours in palettes\n"
			"Dither modes available (and default level):\n"
			"    -dither:none       - No dithering\n"
			"    -dither:floyd,1.0  - Floyd-Steinberg\n"
			"    -dither:ord2,0.5   - 2x2 ordered dithering\n"
			"    -dither:ord4,0.5   - 4x4 ordered dithering\n"
			"    -dither:ord8,0.5   - 8x8 ordered dithering\n"
			"    -dither:ord16,0.5  - 16x16 ordered dithering\n"
			"    -dither:ord32,0.5  - 32x32 ordered dithering\n"
			"    -dither:ord64,0.5  - 64x64 ordered dithering\n"
			"\n"
		);
		return 1;
	}

	int     nPalettes = 16;
	int     nColoursPerPalette = 16;
	int     nUnusedColoursPerPalette = 1;
	int     TileW = 8;
	int     TileH = 8;
	uint8_t BitRange[4] = {0x1F,0x1F,0x1F,0x01};
	int     DitherMode  = DITHER_FLOYDSTEINBERG;
	float   DitherLevel = 1.0f;
	bool    OrderColours = false;
	
	int argi;
	for(argi=3; argi<argc; argi++)
	{
		int ArgOk = 0;

		const char *ArgStr;
		ARGMATCH(argv[argi], "-np:") ArgOk = 1, nPalettes = atoi(ArgStr);
		ARGMATCH(argv[argi], "-ps:") ArgOk = 1, nColoursPerPalette = atoi(ArgStr);
		ARGMATCH(argv[argi], "-tw:") ArgOk = 1, TileW = atoi(ArgStr);
		ARGMATCH(argv[argi], "-th:") ArgOk = 1, TileH = atoi(ArgStr);
		ARGMATCH(argv[argi], "-bgra:")
		{
			ArgOk = 1;
			BitRange[0] = (1 << (*ArgStr++ - '0')) - 1;
			BitRange[1] = (1 << (*ArgStr++ - '0')) - 1;
			BitRange[2] = (1 << (*ArgStr++ - '0')) - 1;
			BitRange[3] = (1 << (*ArgStr++ - '0')) - 1;
		}

		ARGMATCH(argv[argi], "-dither:")
		{
			int d;
	
			DITHERMODE_MATCH(ArgStr, "none",  DITHER_NONE,           0.0f);
			DITHERMODE_MATCH(ArgStr, "floyd", DITHER_FLOYDSTEINBERG, 1.0f);
			DITHERMODE_MATCH(ArgStr, "ord2",  DITHER_ORDERED(1),     0.5f);
			DITHERMODE_MATCH(ArgStr, "ord4",  DITHER_ORDERED(2),     0.5f);
			DITHERMODE_MATCH(ArgStr, "ord8",  DITHER_ORDERED(3),     0.5f);
			DITHERMODE_MATCH(ArgStr, "ord16", DITHER_ORDERED(4),     0.5f);
			DITHERMODE_MATCH(ArgStr, "ord32", DITHER_ORDERED(5),     0.5f);
			DITHERMODE_MATCH(ArgStr, "ord64", DITHER_ORDERED(6),     0.5f);
				
			if(!ArgOk) printf("Unrecognized dither mode: %s\n", ArgStr);
			ArgOk = 1;
		}

		ARGMATCH(argv[argi], "-order")
		{
			ArgOk = 1;
			OrderColours = true;
		}

		if(!ArgOk) printf("Unrecognized argument: %s\n", ArgStr);
	}

	printf("Reading input file...\n");

	struct BmpCtx_t Image;
	if(!BmpCtx_FromFile(&Image, argv[1]))
	{
		printf("Unable to read input file\n");
		return -1;
	}
	
	if(Image.Width%TileW || Image.Height%TileH)
	{
		printf("Image not a multiple of tile size (%dx%d)\n", TileW, TileH);
		BmpCtx_Destroy(&Image);
		return -1;
	}

	struct TilesData_t* TilesData = TilesData_FromBitmap(&Image, TileW, TileH);
	uint8_t *PxData = malloc(Image.Width * Image.Height * sizeof(uint8_t));
	struct BGRAf_t* Palette = calloc(BMP_PALETTE_COLOURS, sizeof(struct BGRAf_t));
	
	if(!TilesData || !PxData || !Palette)
	{
		printf("Out of memory - Image not processed\n");
		free(Palette);
		free(PxData);
		free(TilesData);
		BmpCtx_Destroy(&Image);
		return -1;
	}
	
	struct BGRAf_t RMSE = Qualetize
	(
		&Image,
		TilesData,
		PxData,
		Palette,
		nPalettes,
		nColoursPerPalette,
		nUnusedColoursPerPalette,
		(const struct BGRA8_t *)BitRange,
		DitherMode,
		DitherLevel,
		1,
		OrderColours
	);

	free(TilesData);

#if MEASURE_PSNR
	RMSE.b = -0x1.15F2CFp3f*logf(RMSE.b / 255.0f); //! -20*Log10[RMSE/255] == -20/Log[10] * Log[RMSE/255]
	RMSE.g = -0x1.15F2CFp3f*logf(RMSE.g / 255.0f);
	RMSE.r = -0x1.15F2CFp3f*logf(RMSE.r / 255.0f);
	RMSE.a = -0x1.15F2CFp3f*logf(RMSE.a / 255.0f);
	printf("PSNR = {%.3fdB, %.3fdB, %.3fdB, %.3fdB}\n", RMSE.b, RMSE.g, RMSE.r, RMSE.a);
#else
	(void)RMSE;
#endif

	printf("Writing output file...\n");

	if(!BmpCtx_ToFile(&Image, argv[2]))
	{
		printf("\nUnable to write output file\n\n");
		BmpCtx_Destroy(&Image);
		return -1;
	}

	BmpCtx_Destroy(&Image);
	printf("Done!\n\n");
	return 0;
}
