#pragma once
#include <stdint.h>
#include "colourspace.h"

#define BMP_PALETTE_COLOURS 256

struct BmpCtx_t
{
	int Width, Height;
	struct BGRA8_t *ColPal;
	union
	{
		uint8_t *PxIdx;
		struct BGRA8_t *PxBGR;
	};
};

int BmpCtx_Create(struct BmpCtx_t *Ctx, int w, int h, int PalCol);
void BmpCtx_Destroy(struct BmpCtx_t *Ctx);
int BmpCtx_FromFile(struct BmpCtx_t *Ctx, const char *Filename);
int BmpCtx_ToFile(const struct BmpCtx_t *Ctx, const char *Filename);
