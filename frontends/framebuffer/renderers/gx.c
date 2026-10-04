/* SPDX-License-Identifier: GPL-2.0-only */
/* GX page renderer. Unsupported primitives and RAM targets use libnsfb.
 * GPU/CPU ownership is synchronized by the public Wii libnsfb access hook. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libnsfb.h>
#include <libnsfb_plot.h>
#include "utils/utils.h"
#include "utils/utf8.h"
#include "netsurf/plotters.h"
#include "framebuffer/framebuffer.h"
#include "framebuffer/renderer.h"
#include "framebuffer/font.h"
#include "framebuffer/wii_present.h"

static nsfb_t *target, *screen;
static struct plotter_table plotters;

static bool get_clip(nsfb_bbox_t *clip)
{
	return target == screen && nsfb_plot_get_clip(target, clip) &&
	       clip->x0 < clip->x1 && clip->y0 < clip->y1;
}

static nserror rectangle(const struct redraw_context *ctx,
			 const plot_style_t *style,
			 const struct rect *rect)
{
	nsfb_bbox_t clip, box = {rect->x0, rect->y0, rect->x1, rect->y1};
	if (style->stroke_type != PLOT_OP_TYPE_NONE || !get_clip(&clip))
		return fb_renderer_soft.plotters->rectangle(ctx, style, rect);
	if (style->fill_type == PLOT_OP_TYPE_NONE || box.x0 >= box.x1 ||
	    box.y0 >= box.y1)
		return NSERROR_OK;
	if (wii_gx_fill(target, &clip, &box, style->fill_colour))
		return NSERROR_OK;
	return fb_renderer_soft.plotters->rectangle(ctx, style, rect);
}

static nserror bitmap(const struct redraw_context *ctx,
		      struct bitmap *bitmap,
		      int x,
		      int y,
		      int width,
		      int height,
		      colour bg,
		      bitmap_flags_t flags)
{
	nsfb_bbox_t clip;
	int bw, bh, pitch;
	enum nsfb_format_e format;
	uint8_t *pixels;
	uint64_t hash = 0;
	bool prepared = false;
	bool repeat_x = flags & BITMAPF_REPEAT_X,
	     repeat_y = flags & BITMAPF_REPEAT_Y;
	int64_t left = x, top = y, px, py;
	if (width <= 0 || height <= 0)
		return NSERROR_OK;
	if (!get_clip(&clip))
		goto fallback;
	nsfb_get_geometry((nsfb_t *)bitmap, &bw, &bh, &format);
	if (format != NSFB_FMT_XBGR8888 && format != NSFB_FMT_ABGR8888)
		goto fallback;
	/* Reject unsupported sizes before any tile is submitted. */
	if (bw <= 0 || bh <= 0 || bw > 1024 || bh > 1024)
		goto fallback;
	nsfb_get_buffer((nsfb_t *)bitmap, &pixels, &pitch);
	if (!pixels || pitch < bw * 4)
		goto fallback;
	if (repeat_x)
		left = clip.x0 - ((int64_t)clip.x0 - x) % width;
	if (repeat_y)
		top = clip.y0 - ((int64_t)clip.y0 - y) % height;
	if (repeat_x && left > clip.x0)
		left -= width;
	if (repeat_y && top > clip.y0)
		top -= height;
	for (py = top; py < (repeat_y ? clip.y1 : top + 1); py += height)
		for (px = left; px < (repeat_x ? clip.x1 : left + 1);
		     px += width) {
			nsfb_bbox_t box = {px, py, px + width, py + height};
			if (box.x0 >= clip.x1 || box.x1 <= clip.x0 ||
			    box.y0 >= clip.y1 || box.y1 <= clip.y0)
				continue;
			if (!prepared) {
				hash = wii_gx_image_hash(
					pixels,
					bw,
					bh,
					pitch,
					format == NSFB_FMT_ABGR8888);
				prepared = true;
			}
			if (!wii_gx_image_prehashed(target,
						    &clip,
						    &box,
						    pixels,
						    bw,
						    bh,
						    pitch,
						    format == NSFB_FMT_ABGR8888,
						    hash)) {
				/* Failure may follow successful tiles.
				 * Synchronization makes retrying the whole
				 * alpha image incorrect; finish in software
				 * tile by tile using the same clip instead. */
				nserror error = fb_renderer_soft.plotters
							->bitmap(ctx,
								 bitmap,
								 px,
								 py,
								 width,
								 height,
								 bg,
								 BITMAPF_NONE);
				if (error != NSERROR_OK)
					return error;
			}
		}
	return NSERROR_OK;
fallback:
	return fb_renderer_soft.plotters->bitmap(
		ctx, bitmap, x, y, width, height, bg, flags);
}

#ifdef FB_USE_FREETYPE
static nserror text(const struct redraw_context *ctx,
		    const plot_font_style_t *style,
		    int x,
		    int y,
		    const char *string,
		    size_t length)
{
	nsfb_bbox_t clip;
	size_t next = 0;
	if (!get_clip(&clip))
		return fb_renderer_soft.plotters->text(
			ctx, style, x, y, string, length);
	while (next < length) {
		uint32_t codepoint = utf8_to_ucs4(string + next, length - next);
		FT_Glyph glyph;
		next = utf8_next(string, length, next);
		glyph = fb_getglyph(style, codepoint);
		if (!glyph)
			continue;
		if (glyph->format == FT_GLYPH_FORMAT_BITMAP) {
			FT_BitmapGlyph g = (FT_BitmapGlyph)glyph;
			nsfb_bbox_t box = {x + g->left,
					   y - g->top,
					   x + g->left + g->bitmap.width,
					   y - g->top + g->bitmap.rows};
			if (g->bitmap.width && g->bitmap.rows) {
				if (g->bitmap.pixel_mode ==
				    FT_PIXEL_MODE_MONO) {
					nsfb_plot_glyph1(target,
							 &box,
							 g->bitmap.buffer,
							 g->bitmap.pitch,
							 style->foreground);
				} else if (g->bitmap.pixel_mode !=
						   FT_PIXEL_MODE_GRAY ||
					   !wii_gx_image(target,
							 &clip,
							 &box,
							 g->bitmap.buffer,
							 g->bitmap.width,
							 g->bitmap.rows,
							 g->bitmap.pitch,
							 true,
							 style->foreground,
							 true)) {
					nsfb_plot_glyph8(target,
							 &box,
							 g->bitmap.buffer,
							 g->bitmap.pitch,
							 style->foreground);
				}
			}
		}
		x += glyph->advance.x >> 16;
	}
	return NSERROR_OK;
}
#endif

static nsfb_t *initialise(const char *name, int width, int height, int depth)
{
	plotters = *fb_renderer_soft.plotters;
	plotters.rectangle = rectangle;
	plotters.bitmap = bitmap;
#ifdef FB_USE_FREETYPE
	plotters.text = text;
#endif
	/* GX removes overdraw directly; preserve browser clipping without the
	 * software knockout queue rearranging GPU/CPU transitions. */
	plotters.option_knockout = false;
	screen = target = fb_renderer_soft.initialise(
		name, width, height, depth);
	return screen;
}
static nsfb_t *set_surface(nsfb_t *surface)
{
	nsfb_t *old = fb_renderer_soft.set_surface(surface);
	target = surface;
	return old;
}
static bool start(nsfb_t *surface)
{
	if (!wii_present_start(surface))
		return false;
	/* Non-native dimensions retain correct software rendering/presentation.
	 */
	if (!wii_gx_enable(surface))
		fprintf(stderr,
			"GX page acceleration unavailable; using software plotters with GX presentation\n");
	return true;
}
static bool resize(nsfb_t *surface, int w, int h, int depth)
{
	return fb_renderer_soft.resize(surface, w, h, depth);
}
static void finalise(void)
{
	fb_renderer_soft.finalise();
	screen = target = NULL;
}
static bool set_cursor(struct fbtk_bitmap *cursor)
{
	return fb_renderer_soft.set_cursor(cursor);
}
const struct fb_renderer fb_renderer_gx = {"gx",
					   &plotters,
					   initialise,
					   resize,
					   finalise,
					   set_cursor,
					   set_surface,
					   start,
					   wii_present_frame,
					   wii_present_stop};

/* Exercise the renderer dispatch, not just the low-level GX target check. */
bool framebuffer_gx_test_offscreen(void)
{
	nsfb_t *ram, *previous;
	nsfb_bbox_t box = {0, 0, 16, 16}, sample = {2, 2, 3, 3};
	struct rect rect = {0, 0, 16, 16};
	nsfb_colour_t color = 0;
	plot_style_t style = {.fill_type = PLOT_OP_TYPE_SOLID,
			      .fill_colour = 0xff};
	bool ok;
	if (!screen)
		return true;
	ram = nsfb_new(NSFB_SURFACE_RAM);
	if (!ram)
		return false;
	if (nsfb_set_geometry(ram, 16, 16, NSFB_FMT_XRGB8888) != 0 ||
	    nsfb_init(ram) != 0) {
		nsfb_free(ram);
		return false;
	}
	nsfb_plot_set_clip(ram, &box);
	previous = set_surface(ram);
	ok = rectangle(NULL, &style, &rect) == NSERROR_OK;
	nsfb_plot_readrect(ram, &sample, &color);
	ok &= (color & 0xffffff) == 0xff;
	set_surface(previous);
	nsfb_free(ram);
	return ok;
}
