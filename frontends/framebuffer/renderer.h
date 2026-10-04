/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NETSURF_FB_RENDERER_H
#define NETSURF_FB_RENDERER_H
#include <stdbool.h>
#include <libnsfb.h>

/* Plugins share the browser plotter contract. Bitmaps/offscreen rendering and
 * lifecycle belong to the selected plugin; presentation runs on the main
 * thread. Add compiled plugins to framebuffer.c's registry. Choices select at
 * restart. */
struct fbtk_bitmap;
struct plotter_table;
struct fb_renderer {
	const char *name;
	const struct plotter_table *plotters;
	nsfb_t *(*initialise)(const char *name,
			      int width,
			      int height,
			      int depth);
	bool (*resize)(nsfb_t *surface, int width, int height, int depth);
	void (*finalise)(void);
	bool (*set_cursor)(struct fbtk_bitmap *cursor);
	nsfb_t *(*set_surface)(nsfb_t *surface);
	bool (*start_presenter)(nsfb_t *surface);
	void (*present)(void);
	void (*stop_presenter)(void);
};
extern const struct fb_renderer fb_renderer_soft, fb_renderer_soft_legacy;
#ifdef GEKKO
extern const struct fb_renderer fb_renderer_gx;
#endif
#endif
