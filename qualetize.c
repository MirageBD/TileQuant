#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "bitmap.h"
#include "colourspace.h"
#include "qualetize.h"
#include "tiles.h"

#define MEASURE_PSNR 1

static int FindPaletteEntry(const struct BGRAf_t *Px, const struct BGRAf_t *Pal, int MaxPalSize, int PalUnused)
{
	int   i;
	int   MinIdx = 0;
	float MinDst = 8.0e37f;
	for(i=PalUnused-1; i<MaxPalSize; i++)
	{
		float Dst = BGRAf_ColDistance(Px, &Pal[i]);
		if(Dst < MinDst)
		{
			MinIdx = i;
			MinDst = Dst;
		}
	}
	return MinIdx;
}

static void OrderPalettes(struct BGRAf_t *Pal, int MaxTilePals, int MaxPalSize)
{
	int   i, j;
	bool swapped = true;
	
	while(swapped == true)
	{
		swapped = false;
		for(i=0; i<MaxTilePals; i++)
		{
			for(j=0; j<MaxPalSize-1; j++)
			{
				struct BGRAf_t p1 = BGRAf_FromYCoCg(&Pal[i * MaxPalSize + j + 0]);
				struct BGRAf_t p2 = BGRAf_FromYCoCg(&Pal[i * MaxPalSize + j + 1]);
				
				float i1 = BGRAf_Dot(&(const struct BGRAf_t) {0.33f, 0.33f, 0.33f, 0.0f}, &p1);
				float i2 = BGRAf_Dot(&(const struct BGRAf_t) {0.33f, 0.33f, 0.33f, 0.0f}, &p2);
				
				if(i1 > i2)
				{
					swapped = true;
					Pal[i * MaxPalSize + j + 0] = BGRAf_AsYCoCg(&p2);
					Pal[i * MaxPalSize + j + 1] = BGRAf_AsYCoCg(&p1);
				}
			}
		}
	}
}

struct BGRAf_t Qualetize(
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
	bool  OrderColours
) {
	int i;

	TilesData_QuantizePalettes(TilesData, Palette, MaxTilePals, MaxPalSize, PalUnused);

	struct BGRAf_t DitherVal = BGRAf_FromBGRA(&(const struct BGRA8_t){1,1,1,0}, BitRange);
	DitherVal = BGRAf_Muli(&DitherVal, 0.25f);
	for(i=0; i<MaxTilePals*MaxPalSize; i++)
	{
		struct BGRAf_t p = BGRAf_FromYCoCg(&Palette[i]);

		if(i&1)
			p = BGRAf_Add(&p, &DitherVal);

		struct BGRA8_t p2 = BGRA_FromBGRAf(&p, BitRange);
		p = BGRAf_FromBGRA(&p2, BitRange);
		Palette[i] = BGRAf_AsYCoCg(&p);
	}

	int x, y;
	int ImgW = Image->Width;
	int ImgH = Image->Height;
	int TileW = TilesData->TileW;
	int TileH = TilesData->TileH;
	int32_t *TilePalIdx = TilesData->TilePalIdx;
#if MEASURE_PSNR
	      struct BGRAf_t  RMSE     = (struct BGRAf_t){0,0,0,0};
#endif
	const        uint8_t *PxSrcIdx = Image->ColPal ? Image->PxIdx  : NULL;
	const struct BGRA8_t *PxSrcBGR = Image->ColPal ? Image->ColPal : Image->PxBGR;

	struct BGRAf_t *PxDiffuse     = TilesData->PxData;
	struct BGRAf_t *PaletteSpread = TilesData->PxData;
	if(DitherType != DITHER_NONE)
	{
		if(DitherType == DITHER_FLOYDSTEINBERG)
		{
			for(y=0;y<ImgH;y++)
			{
				for(x=0;x<ImgW;x++)
				{
					PxDiffuse[y*ImgW+x] = (struct BGRAf_t){0,0,0,0};
				}
			}
		}
		else
		{
			for(i=0;i<MaxTilePals;i++)
			{
				int n;
				struct BGRAf_t Mean = (struct BGRAf_t){0,0,0,0};
				for(n=PalUnused;n<MaxPalSize;n++) Mean = BGRAf_Add(&Mean, &Palette[i*MaxPalSize+n]);
				Mean = BGRAf_Divi(&Mean, MaxPalSize-PalUnused);

				struct BGRAf_t Spread = {0,0,0,0}, SpreadW = {0,0,0,0};
				for(n=PalUnused; n<MaxPalSize; n++)
				{
					struct BGRAf_t d = BGRAf_Sub(&Palette[i*MaxPalSize+n], &Mean);
							   d = BGRAf_Abs(&d);
					struct BGRAf_t w = BGRAf_Sqrt(&d);
							   d = BGRAf_Mul(&d, &w);
					Spread  = BGRAf_Add(&Spread,  &d);
					SpreadW = BGRAf_Add(&SpreadW, &w);
				}
				
				Spread = BGRAf_DivSafe(&Spread, &SpreadW, NULL);
	#ifdef DITHER_NO_ALPHA
				Spread.a = 0.0f;
	#endif
				PaletteSpread[i] = BGRAf_Muli(&Spread, DitherLevel);
			}
		}
	}

	if(OrderColours)
	{
		OrderPalettes(Palette, MaxTilePals, MaxPalSize);
	}

	for(y=0;y<ImgH;y++)
	{
		for(x=0;x<ImgW;x++)
		{
			int PalIdx = TilePalIdx[(y/TileH)*(ImgW/TileW) + (x/TileW)];

			struct BGRAf_t Px, Px_Original;

			struct BGRA8_t p;
			if(PxSrcIdx) p = PxSrcBGR[PxSrcIdx[y*ImgW + x]];
			else         p = PxSrcBGR[         y*ImgW + x ];
			Px_Original = BGRAf_FromBGRA8(&p);
			Px_Original = BGRAf_AsYCoCg(&Px_Original);
			Px = Px_Original;

			if(DitherType != DITHER_NONE)
			{
				if(DitherType == DITHER_FLOYDSTEINBERG)
				{
					struct BGRAf_t Dif = PxDiffuse[y*ImgW + x];
	#ifdef DITHER_NO_ALPHA
					Dif.a = 0.0f;
	#endif
					Dif = BGRAf_Muli(&Dif, DitherLevel);
					Px  = BGRAf_Add (&Px, &Dif);
				}
				else
				{
					int Threshold = 0, xKey = x, yKey = x^y;
					int Bit = DitherType-1; do {
						Threshold = Threshold*2 + (yKey & 1), yKey >>= 1;
						Threshold = Threshold*2 + (xKey & 1), xKey >>= 1;
					} while(--Bit >= 0);
					float fThres = Threshold * (1.0f / (1 << (2*DitherType))) - 0.5f;
					struct BGRAf_t DitherVal = BGRAf_Muli(&PaletteSpread[PalIdx], fThres);
					Px = BGRAf_Add(&Px, &DitherVal);
				}
			}

			int PalCol = FindPaletteEntry(&Px, Palette + PalIdx*MaxPalSize, MaxPalSize, PalUnused);
			PxData[y*ImgW + x] = PalIdx*MaxPalSize + PalCol;
			struct BGRAf_t Error = BGRAf_Sub(&Px_Original, &Palette[PxData[y*ImgW + x]]);

			if(DitherType == DITHER_FLOYDSTEINBERG)
			{
				if(y+1 < ImgH)
				{
					if(x > 0)
					{
						struct BGRAf_t t = BGRAf_Muli(&Error, 3.0f/16);
						PxDiffuse[(y+1)*ImgW+(x-1)] = BGRAf_Add(&PxDiffuse[(y+1)*ImgW+(x-1)], &t);
					}
					if(1)
					{
						struct BGRAf_t t = BGRAf_Muli(&Error, 5.0f/16);
						PxDiffuse[(y+1)*ImgW+(x  )] = BGRAf_Add(&PxDiffuse[(y+1)*ImgW+(x  )], &t);
					}
					if(x+1 < ImgW)
					{
						struct BGRAf_t t = BGRAf_Muli(&Error, 1.0f/16);
						PxDiffuse[(y+1)*ImgW+(x+1)] = BGRAf_Add(&PxDiffuse[(y+1)*ImgW+(x+1)], &t);
					}
				}
				if(x+1 < ImgW)
				{
						struct BGRAf_t t = BGRAf_Muli(&Error, 7.0f/16);
						PxDiffuse[(y  )*ImgW+(x+1)] = BGRAf_Add(&PxDiffuse[(y  )*ImgW+(x+1)], &t);
				}
			}
			
			#if MEASURE_PSNR
				Error = BGRAf_FromYCoCg(&Error);
				Error = BGRAf_Mul(&Error, &Error);
				RMSE  = BGRAf_Add(&RMSE, &Error);
			#endif
		}
	}

	struct BGRA8_t *PalBGR = (struct BGRA8_t*)Palette;
	for(i=0; i<BMP_PALETTE_COLOURS; i++)
	{
		struct BGRAf_t x = BGRAf_FromYCoCg(&Palette[i]);
		PalBGR[i] = BGRA8_FromBGRAf(&x);
	}

	if(ReplaceImage)
	{
		if(Image->ColPal)
		{
			free(Image->ColPal);
			free(Image->PxIdx);
		}
		else
		{
			free(Image->PxBGR);
		}
		Image->ColPal = PalBGR;
		Image->PxIdx  = PxData;
	}

	#if MEASURE_PSNR
		RMSE = BGRAf_Divi(&RMSE, ImgW*ImgH);
		RMSE = BGRAf_Sqrt(&RMSE);
		return RMSE;
	#else
		return (struct BGRAf_t){0,0,0,0};
	#endif
}
