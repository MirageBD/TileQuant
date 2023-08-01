#pragma once

#include "bitmap.h"
#include "colourspace.h"
#include "tiles.h"

#define DITHER_NONE           ( 0)
#define DITHER_ORDERED(n)     ( n)
#define DITHER_FLOYDSTEINBERG (-1)
#define DITHER_NO_ALPHA

struct BGRAf_t Qualetize
(
	struct BmpCtx_t *Image,
	struct TilesData_t *TilesData,
	uint8_t *PxData,
	struct BGRAf_t *Palette,
	int   MaxTilePals,
	int   MaxPalSize,
	int   PalUnused,
	const struct BGRA8_t *BitRange,
	int   DitherType,
	float DitherLevel,
	int   ReplaceImage,
	bool  Order
);
