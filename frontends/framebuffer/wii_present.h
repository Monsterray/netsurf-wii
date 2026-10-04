/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NETSURF_WII_PRESENT_H
#define NETSURF_WII_PRESENT_H
#include <stdbool.h>
#include <stdio.h>
#include <libnsfb.h>
bool wii_present_start(nsfb_t *surface);
void wii_present_frame(void);
void wii_present_stop(void);
bool wii_present_test_gx(void);
bool wii_present_test_cursor(void);
bool wii_present_capture(const char *path);
void wii_present_redraw(bool start);
void wii_present_profile_checkpoint(void);
void wii_present_stats(FILE *report);
/* Native claims preserve GPU ownership. CPU readers must reacquire through
 * nsfb_get_buffer or the normal libnsfb plot/read APIs. */
int wii_gx_claim(nsfb_t *surface, nsfb_bbox_t *box);
bool wii_gx_enable(nsfb_t *surface);
bool wii_gx_fill(nsfb_t *surface,
		 const nsfb_bbox_t *clip,
		 const nsfb_bbox_t *box,
		 uint32_t color);
bool wii_gx_image(nsfb_t *surface,
		  const nsfb_bbox_t *clip,
		  const nsfb_bbox_t *box,
		  const uint8_t *pixels,
		  int width,
		  int height,
		  int pitch,
		  bool alpha,
		  uint32_t color,
		  bool glyph);
/* A prepared hash is reusable only within a single immutable bitmap plot. */
uint64_t wii_gx_image_hash(const uint8_t *pixels,
			   int width,
			   int height,
			   int pitch,
			   bool alpha);
bool wii_gx_image_prehashed(nsfb_t *surface,
			    const nsfb_bbox_t *clip,
			    const nsfb_bbox_t *box,
			    const uint8_t *pixels,
			    int width,
			    int height,
			    int pitch,
			    bool alpha,
			    uint64_t hash);
#endif
