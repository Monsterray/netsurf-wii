/* SPDX-License-Identifier: GPL-2.0-only */
/* CPU framebuffer -> GX texture conversion shared with the host checks. */
#ifndef NETSURF_WII_TILES_H
#define NETSURF_WII_TILES_H
#include <stdint.h>
#include <stddef.h>
static inline void
wii_convert_tile(uint8_t *target, const uint8_t *source, int pitch, int bpp)
{
	int x, y;
	for (y = 0; y < 4; y++) {
		const uint8_t *row = source + y * pitch;
		for (x = 0; x < 4; x++) {
			int p = (y * 4 + x) * 2;
			if (bpp == 16) {
				target[p] = row[x * 2];
				target[p + 1] = row[x * 2 + 1];
			} else {
				target[p] = row[x * 4]; /* alpha/red plane */
				target[p + 1] = row[x * 4 + 1];
				target[p + 32] = row[x * 4 + 2];
				target[p + 33] = row[x * 4 + 3];
			}
		}
	}
}
#endif
