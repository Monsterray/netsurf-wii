/* Host regression for the actual GEKKO blitters, including padded rows. */
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "nsfb.h"
#include "plot.h"

extern const nsfb_plotter_fns_t _nsfb_32bpp_xrgb8888_plotters;
static unsigned int allocations, synchronizations;
static const nsfb_bbox_t *synchronized_region;
static bool synchronized_writing;
static void synchronize(void *context, const nsfb_bbox_t *region, bool writing)
{
	(void)context;
	synchronizations++;
	synchronized_region = region;
	synchronized_writing = writing;
}
void *tracked_malloc(size_t size)
{
	allocations++;
	return NULL;
}

int main(void)
{
	unsigned char rgba[32] = {255, 0,  0,	255, 0,	  255, 0,   255,
				  99,  99, 99,	99,  99,  99,  99,  99,
				  0,   0,  255, 255, 255, 255, 255, 255};
	uint32_t output[16] = {0};
	nsfb_t src = {0}, dst = {0};
	nsfb_bbox_t loc = {0, 0, 2, 2};
	src.width = src.height = 2;
	src.format = NSFB_FMT_ABGR8888;
	src.bpp = 32;
	src.linelen = 16;
	src.ptr = rgba;
	dst.width = dst.height = 4;
	dst.bpp = 32;
	dst.linelen = 16;
	dst.ptr = (uint8_t *)output;
	dst.clip = (nsfb_bbox_t){0, 0, 4, 4};
	dst.plotter_fns = (nsfb_plotter_fns_t *)&_nsfb_32bpp_xrgb8888_plotters;
	assert(nsfb_plot_copy(&src, NULL, &dst, &loc));
	assert(output[0] == 0xffff0000 && output[1] == 0xff00ff00);
	assert(output[4] == 0xff0000ff && output[5] == 0xffffffff);
	assert(allocations == 0);
	loc = (nsfb_bbox_t){0, 0, 4, 4};
	assert(nsfb_plot_copy(&src, NULL, &dst, &loc));
	assert(output[0] == 0xffff0000 && output[3] == 0xff00ff00);
	assert(output[12] == 0xff0000ff && output[15] == 0xffffffff);
	loc = (nsfb_bbox_t){0, 0, 2, 2};
	assert(nsfb_plot_bitmap_tiles_rgba(
		&dst, &loc, 2, 2, (const nsfb_colour_t *)rgba, 2, 2, 4, true));
	assert(output[8] == 0xffff0000 && output[15] == 0xffffffff);
	assert(!dst.bitmap_src_rgba && allocations == 0);
	/* Canonical UI/cursor colors must remain canonical after an image blit.
	 */
	nsfb_colour_t cursor = 0xff0000ff;
	loc = (nsfb_bbox_t){0, 0, 1, 1};
	assert(nsfb_plot_bitmap(&dst, &loc, &cursor, 1, 1, 1, false));
	assert(output[0] == 0xffff0000);
	dst.wii_sync = synchronize;
	synchronizations = 0;
	assert(nsfb_plot_rectangle_fill(&dst, &loc, 0xff));
	assert(synchronizations == 1 && synchronized_region == &loc &&
	       synchronized_writing);
	nsfb_colour_t read;
	assert(nsfb_plot_readrect(&dst, &loc, &read));
	assert(synchronizations == 2 && synchronized_region == &loc &&
	       !synchronized_writing);
	src.wii_sync = synchronize;
	assert(nsfb_plot_copy(&src, &loc, &dst, &loc));
	assert(synchronizations == 4 && synchronized_region == &loc &&
	       synchronized_writing);
	return 0;
}
