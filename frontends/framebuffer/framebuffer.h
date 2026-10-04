/*
 * Copyright 2008 Vincent Sanders <vince@simtec.co.uk>
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

/**
 * \file
 * framebuffer interface.
 */

#ifndef NETSURF_FB_FRAMEBUFFER_H
#define NETSURF_FB_FRAMEBUFFER_H

struct fbtk_bitmap;
const struct plotter_table *framebuffer_get_plotters(void);
const char *framebuffer_renderer_name(void);
void framebuffer_present(void);

nsfb_t *framebuffer_initialise(const char *fename, int width, int height, int bpp);
/* Plot-API claim; raw CPU users must call nsfb_claim/nsfb_get_buffer. */
int framebuffer_claim(nsfb_t *, nsfb_bbox_t *);
bool framebuffer_resize(nsfb_t *nsfb, int width, int height, int bpp);
void framebuffer_finalise(void);
bool framebuffer_set_cursor(struct fbtk_bitmap *bm);

/** Set framebuffer surface to render into
 *
 * @return return old surface
 */
nsfb_t *framebuffer_set_surface(nsfb_t *new_nsfb);

#ifdef GEKKO
bool framebuffer_gx_test_offscreen(void);
#endif
#endif
