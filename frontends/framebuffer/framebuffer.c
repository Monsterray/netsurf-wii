/*
 * Copyright 2008 Vincent Sanders <vince@simtec.co.uk>
 *
 * Framebuffer interface
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Renderer plugins are compiled into the DOL and selected at startup. */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <libnsfb.h>
#include "utils/nsoption.h"
#include "netsurf/plotters.h"
#include "framebuffer/framebuffer.h"
#include "framebuffer/renderer.h"
#ifdef GEKKO
#include "framebuffer/wii_present.h"
#endif
static const struct fb_renderer *renderer = &fb_renderer_soft;
#ifdef GEKKO
static const struct fb_renderer *const plugins[] = {
	&fb_renderer_soft,
	&fb_renderer_soft_legacy,
	&fb_renderer_gx,
};
#endif

const struct plotter_table *framebuffer_get_plotters(void)
{
	return renderer->plotters;
}
const char *framebuffer_renderer_name(void)
{
	return renderer->name;
}
nsfb_t *framebuffer_initialise(const char *name, int width, int height, int bpp)
{
	nsfb_t *surface;
#ifdef GEKKO
	const char *selected = nsoption_charp(fb_renderer);
	renderer = &fb_renderer_soft;
	unsigned i;
	bool found = selected == NULL;
	for (i = 0; i < sizeof(plugins) / sizeof(plugins[0]); i++) {
		if (selected != NULL &&
		    strcmp(selected, plugins[i]->name) == 0) {
			renderer = plugins[i];
			found = true;
			break;
		}
	}
	if (!found) {
		fprintf(stderr,
			"Unknown renderer '%s'; using soft\n",
			selected);
		renderer = &fb_renderer_soft;
	}
	if (bpp != 16 && bpp != 32)
		return NULL;
#endif
	surface = renderer->initialise(name, width, height, bpp);
#ifdef GEKKO
	if (surface != NULL && renderer->start_presenter != NULL &&
	    !renderer->start_presenter(surface)) {
		fprintf(stderr,
			"Presenter allocation failed; using soft-legacy\n");
		renderer = &fb_renderer_soft_legacy;
	}
#endif
	return surface;
}
bool framebuffer_resize(nsfb_t *surface, int width, int height, int bpp)
{
#ifdef GEKKO
	/* Wii has a fixed output mode; rebuilding SDL would restart its GX
	 * owner. */
	int old_width, old_height;
	nsfb_get_geometry(surface, &old_width, &old_height, NULL);
	return width == old_width && height == old_height;
#else
	return renderer->resize(surface, width, height, bpp);
#endif
}
void framebuffer_present(void)
{
#ifdef GEKKO
	if (renderer->present != NULL)
		renderer->present();
#endif
}
void framebuffer_finalise(void)
{
#ifdef GEKKO
	if (renderer->stop_presenter != NULL)
		renderer->stop_presenter();
#endif
	renderer->finalise();
}
bool framebuffer_set_cursor(struct fbtk_bitmap *bitmap)
{
	return renderer->set_cursor(bitmap);
}
nsfb_t *framebuffer_set_surface(nsfb_t *surface)
{
	return renderer->set_surface(surface);
}

int framebuffer_claim(nsfb_t *surface, nsfb_bbox_t *box)
{
#ifdef GEKKO
	return wii_gx_claim(surface, box);
#else
	return nsfb_claim(surface, box);
#endif
}
