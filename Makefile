all:
	$(CC) -lm -O2 -Wall -Wextra Bitmap.c Quantize.c Qualetize.c Tiles.c tilequant.c -o tilequant

test:
	./tilequant in.bmp out.bmp -np:16 -ps:16 -tw:16 -th:8 -dither:ord2,0.5 -order

.PHONY: clean
clean:
	rm -rf ./tilequant
