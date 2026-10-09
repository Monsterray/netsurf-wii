/*
 * Copyright 2008, 2014 Vincent Sanders <vince@netsurf-browser.org>
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

#include <stdint.h>
#include <limits.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdlib.h>
#include <nsutils/time.h>

#include <libnsfb.h>
#include <libnsfb_plot.h>
#include <libnsfb_event.h>

#ifdef GEKKO
#include <fat.h>
#include <sys/stat.h>
#include <malloc.h>
#include <ogc/system.h>
#include <wiisocket.h>
#include <unistd.h>
#include "framebuffer/wii_compat.h"
#include "framebuffer/wii_present.h"
#include "framebuffer/wii_agent.h"
#ifdef NETSURF_HBC_AGENT
#define WII_LOG(...) do { \
	SYS_Report("NetSurf Wii: " __VA_ARGS__); \
	fprintf(stderr, "NetSurf Wii: " __VA_ARGS__); \
} while (0)
#else
#define WII_LOG(...) SYS_Report("NetSurf Wii: " __VA_ARGS__)
#endif
#else
#define WII_LOG(...) ((void)0)
#endif

#include "utils/utils.h"
#include "utils/nsoption.h"
#include "utils/filepath.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "netsurf/browser_window.h"
#include "netsurf/keypress.h"
#include "desktop/browser_history.h"
#include "netsurf/plotters.h"
#include "netsurf/window.h"
#include "netsurf/misc.h"
#include "netsurf/netsurf.h"
#include "netsurf/cookie_db.h"
#include "content/fetch.h"
#include "content/content.h"
#include "netsurf/content.h"
#ifdef GEKKO
#include <dom/dom.h>
#include "html/html_save.h"
#include "html/box.h"
#include "html/box_inspect.h"
#include "content/backing_store.h"
#include "content/request_filter.h"
#endif

#if defined(GEKKO) && defined(WITH_PDF_EXPORT)
#include "desktop/font_haru.h"
#include "desktop/print.h"
#include "desktop/save_pdf.h"
#endif

#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"
#include "framebuffer/framebuffer.h"
#include "framebuffer/schedule.h"
#include "framebuffer/findfile.h"
#include "framebuffer/image_data.h"
#include "framebuffer/font.h"
#include "framebuffer/clipboard.h"
#include "framebuffer/fetch.h"
#include "framebuffer/bitmap.h"
#include "framebuffer/local_history.h"
#include "framebuffer/corewindow.h"


#define NSFB_TOOLBAR_DEFAULT_LAYOUT "blfsrutc"

fbtk_widget_t *fbtk;

static bool fb_complete = false;

struct gui_window *input_window = NULL;
struct gui_window *search_current_window;
struct gui_window *window_list = NULL;

/* private data for browser user widget */
struct browser_widget_s {
	struct browser_window *bw; /**< The browser window connected to this gui window */
	int scrollx, scrolly; /**< scroll offsets. */

	/* Pending window redraw state. */
	bool redraw_required; /**< flag indicating the foreground loop
			       * needs to redraw the browser widget.
			       */
	bbox_t redraw_box; /**< Area requiring redraw. */
	bool pan_required; /**< flag indicating the foreground loop
			    * needs to pan the window.
			    */
	int panx, pany; /**< Panning required. */
};

static struct gui_drag {
	enum state {
		GUI_DRAG_NONE,
		GUI_DRAG_PRESSED,
		GUI_DRAG_DRAG
	} state;
	int button;
	int x;
	int y;
	bool grabbed_pointer;
} gui_drag;


/**
 * Cause an abnormal program termination.
 *
 * \note This never returns and is intended to terminate without any cleanup.
 *
 * \param error The message to display to the user.
 */
static void die(const char *error)
{
	fprintf(stderr, "%s\n", error);
	exit(1);
}


/**
 * Warn the user of an event.
 *
 * \param[in] warning A warning looked up in the message translation table
 * \param[in] detail Additional text to be displayed or NULL.
 * \return NSERROR_OK on success or error code if there was a
 *           faliure displaying the message to the user.
 */
static nserror fb_warn_user(const char *warning, const char *detail)
{
	NSLOG(netsurf, INFO, "%s %s", warning, detail);
	return NSERROR_OK;
}

/* queue a redraw operation, co-ordinates are relative to the window */
static void
fb_queue_redraw(struct fbtk_widget_s *widget, int x0, int y0, int x1, int y1)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);

	bwidget->redraw_box.x0 = min(bwidget->redraw_box.x0, x0);
	bwidget->redraw_box.y0 = min(bwidget->redraw_box.y0, y0);
	bwidget->redraw_box.x1 = max(bwidget->redraw_box.x1, x1);
	bwidget->redraw_box.y1 = max(bwidget->redraw_box.y1, y1);

	if (fbtk_clip_to_widget(widget, &bwidget->redraw_box)) {
		bwidget->redraw_required = true;
		fbtk_request_redraw(widget);
	} else {
		bwidget->redraw_box.y0 = bwidget->redraw_box.x0 = INT_MAX;
		bwidget->redraw_box.y1 = bwidget->redraw_box.x1 = -(INT_MAX);
		bwidget->redraw_required = false;
	}
}

/* queue a window scroll */
static void
widget_scroll_y(struct gui_window *gw, int y, bool abs)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);
	int content_width, content_height;
	int height;

	NSLOG(netsurf, DEEPDEBUG, "window scroll");
	if (abs) {
		bwidget->pany = y - bwidget->scrolly;
	} else {
		bwidget->pany += y;
	}

	browser_window_get_extents(gw->bw, true,
			&content_width, &content_height);

	height = fbtk_get_height(gw->browser);

	/* dont pan off the top */
	if ((bwidget->scrolly + bwidget->pany) < 0)
		bwidget->pany = -bwidget->scrolly;

	/* do not pan off the bottom of the content */
	if ((bwidget->scrolly + bwidget->pany) > (content_height - height))
		bwidget->pany = (content_height - height) - bwidget->scrolly;

	if (bwidget->pany == 0)
		return;

	bwidget->pan_required = true;

	fbtk_request_redraw(gw->browser);

	fbtk_set_scroll_position(gw->vscroll, bwidget->scrolly + bwidget->pany);
}

/* queue a window scroll */
static void
widget_scroll_x(struct gui_window *gw, int x, bool abs)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);
	int content_width, content_height;
	int width;

	if (abs) {
		bwidget->panx = x - bwidget->scrollx;
	} else {
		bwidget->panx += x;
	}

	browser_window_get_extents(gw->bw, true,
			&content_width, &content_height);

	width = fbtk_get_width(gw->browser);

	/* dont pan off the left */
	if ((bwidget->scrollx + bwidget->panx) < 0)
		bwidget->panx = - bwidget->scrollx;

	/* do not pan off the right of the content */
	if ((bwidget->scrollx + bwidget->panx) > (content_width - width))
		bwidget->panx = (content_width - width) - bwidget->scrollx;

	if (bwidget->panx == 0)
		return;

	bwidget->pan_required = true;

	fbtk_request_redraw(gw->browser);

	fbtk_set_scroll_position(gw->hscroll, bwidget->scrollx + bwidget->panx);
}

static void
fb_pan(fbtk_widget_t *widget,
       struct browser_widget_s *bwidget,
       struct browser_window *bw)
{
	int x;
	int y;
	int width;
	int height;
	nsfb_bbox_t srcbox;
	nsfb_bbox_t dstbox;

	nsfb_t *nsfb = fbtk_get_nsfb(widget);

	height = fbtk_get_height(widget);
	width = fbtk_get_width(widget);

	NSLOG(netsurf, DEEPDEBUG, "panning %d, %d",
			bwidget->panx, bwidget->pany);

	x = fbtk_get_absx(widget);
	y = fbtk_get_absy(widget);

	/* if the pan exceeds the viewport size just redraw the whole area */
	if (bwidget->pany >= height || bwidget->pany <= -height ||
	    bwidget->panx >= width || bwidget->panx <= -width) {

		bwidget->scrolly += bwidget->pany;
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, 0, 0, width, height);

		/* ensure we don't try to scroll again */
		bwidget->panx = 0;
		bwidget->pany = 0;
		bwidget->pan_required = false;
		return;
	}

	if (bwidget->pany < 0) {
		/* pan up by less then viewport height */
		srcbox.x0 = x;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width;
		srcbox.y1 = srcbox.y0 + height + bwidget->pany;

		dstbox.x0 = x;
		dstbox.y0 = y - bwidget->pany;
		dstbox.x1 = dstbox.x0 + width;
		dstbox.y1 = dstbox.y0 + height + bwidget->pany;

		/* move part that remains visible up */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrolly += bwidget->pany;
		fb_queue_redraw(widget, 0, 0, width, - bwidget->pany);

	} else if (bwidget->pany > 0) {
		/* pan down by less then viewport height */
		srcbox.x0 = x;
		srcbox.y0 = y + bwidget->pany;
		srcbox.x1 = srcbox.x0 + width;
		srcbox.y1 = srcbox.y0 + height - bwidget->pany;

		dstbox.x0 = x;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width;
		dstbox.y1 = dstbox.y0 + height - bwidget->pany;

		/* move part that remains visible down */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrolly += bwidget->pany;
		fb_queue_redraw(widget, 0, height - bwidget->pany,
				width, height);
	}

	if (bwidget->panx < 0) {
		/* pan left by less then viewport width */
		srcbox.x0 = x;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width + bwidget->panx;
		srcbox.y1 = srcbox.y0 + height;

		dstbox.x0 = x - bwidget->panx;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width + bwidget->panx;
		dstbox.y1 = dstbox.y0 + height;

		/* move part that remains visible left */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, 0, 0, -bwidget->panx, height);

	} else if (bwidget->panx > 0) {
		/* pan right by less then viewport width */
		srcbox.x0 = x + bwidget->panx;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width - bwidget->panx;
		srcbox.y1 = srcbox.y0 + height;

		dstbox.x0 = x;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width - bwidget->panx;
		dstbox.y1 = dstbox.y0 + height;

		/* move part that remains visible right */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, width - bwidget->panx, 0,
				width, height);
	}

	bwidget->pan_required = false;
	bwidget->panx = 0;
	bwidget->pany = 0;
}

static void
fb_redraw(fbtk_widget_t *widget,
	  struct browser_widget_s *bwidget,
	  struct browser_window *bw)
{
	int x;
	int y;
	int caret_x, caret_y, caret_h;
	struct rect clip;
	struct redraw_context ctx = {.interactive = true,
				     .background_images = true,
				     .plot = framebuffer_get_plotters()};
	nsfb_t *nsfb = fbtk_get_nsfb(widget);

	x = fbtk_get_absx(widget);
	y = fbtk_get_absy(widget);

	/* adjust clipping co-ordinates according to window location */
	bwidget->redraw_box.y0 += y;
	bwidget->redraw_box.y1 += y;
	bwidget->redraw_box.x0 += x;
	bwidget->redraw_box.x1 += x;

	framebuffer_claim(nsfb, &bwidget->redraw_box);

	/* redraw bounding box is relative to window */
	clip.x0 = bwidget->redraw_box.x0;
	clip.y0 = bwidget->redraw_box.y0;
	clip.x1 = bwidget->redraw_box.x1;
	clip.y1 = bwidget->redraw_box.y1;

	browser_window_redraw(bw,
			x - bwidget->scrollx,
			y - bwidget->scrolly,
			&clip, &ctx);

	if (fbtk_get_caret(widget, &caret_x, &caret_y, &caret_h)) {
		/* This widget has caret, so render it */
		nsfb_bbox_t line;
		nsfb_plot_pen_t pen;

		line.x0 = x - bwidget->scrollx + caret_x;
		line.y0 = y - bwidget->scrolly + caret_y;
		line.x1 = x - bwidget->scrollx + caret_x;
		line.y1 = y - bwidget->scrolly + caret_y + caret_h;

		pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
		pen.stroke_width = 1;
		pen.stroke_colour = 0xFF0000FF;

		nsfb_plot_line(nsfb, &line, &pen);
	}

	nsfb_update(fbtk_get_nsfb(widget), &bwidget->redraw_box);

	bwidget->redraw_box.y0 = bwidget->redraw_box.x0 = INT_MAX;
	bwidget->redraw_box.y1 = bwidget->redraw_box.x1 = INT_MIN;
	bwidget->redraw_required = false;
}

static int
fb_browser_window_redraw(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget;

	bwidget = fbtk_get_userpw(widget);
	if (bwidget == NULL) {
		NSLOG(netsurf, INFO,
		      "browser widget from widget %p was null", widget);
		return -1;
	}

	if (bwidget->pan_required) {
		fb_pan(widget, bwidget, gw->bw);
	}

	if (bwidget->redraw_required) {
		fb_redraw(widget, bwidget, gw->bw);
	} else {
		bwidget->redraw_box.x0 = 0;
		bwidget->redraw_box.y0 = 0;
		bwidget->redraw_box.x1 = fbtk_get_width(widget);
		bwidget->redraw_box.y1 = fbtk_get_height(widget);
		fb_redraw(widget, bwidget, gw->bw);
	}
	return 0;
}

static int fb_browser_window_destroy(fbtk_widget_t *widget,
		fbtk_callback_info *cbi)
{
	struct browser_widget_s *browser_widget;

	if (widget == NULL) {
		return 0;
	}

	/* Free private data */
	browser_widget = fbtk_get_userpw(widget);
	free(browser_widget);

	return 0;
}

static void
framebuffer_surface_iterator(void *ctx, const char *name, enum nsfb_type_e type)
{
	const char *arg0 = ctx;

	fprintf(stderr, "%s: %s\n", arg0, name);
}

static enum nsfb_type_e fetype = NSFB_SURFACE_COUNT;
static const char *fename;
static int febpp;
static int fewidth;
static int feheight;
static const char *feurl;


static void
framebuffer_pick_default_fename(void *ctx, const char *name, enum nsfb_type_e type)
{
	if (type < fetype) {
		fename = name;
	}
}

static bool
process_cmdline(int argc, char** argv)
{
	int opt;
	int option_index;
	static struct option long_options[] = {
		{0, 0, 0,  0 }
	}; /* no long options */

	NSLOG(netsurf, INFO, "argc %d, argv %p", argc, argv);

	nsfb_enumerate_surface_types(framebuffer_pick_default_fename, NULL);

	febpp = nsoption_int(fb_depth);

	fewidth = nsoption_int(window_width);
	if (fewidth <= 0) {
#ifdef GEKKO
		fewidth = 640;
#else
		fewidth = 800;
#endif
	}
	feheight = nsoption_int(window_height);
	if (feheight <= 0) {
#ifdef GEKKO
		feheight = 480;
#else
		feheight = 600;
#endif
	}

#ifdef GEKKO
	/* Do not depend on static-library constructor order on Wii. */
	fename = "sdl";
#endif

	if ((nsoption_charp(homepage_url) != NULL) && 
	    (nsoption_charp(homepage_url)[0] != '\0')) {
		feurl = nsoption_charp(homepage_url);
	} else {
		feurl = NETSURF_HOMEPAGE;
	}

	while((opt = getopt_long(argc, argv, "f:b:w:h:",
				 long_options, &option_index)) != -1) {
		switch (opt) {
		case 'f':
			fename = optarg;
			break;

		case 'b':
			febpp = atoi(optarg);
			break;

		case 'w':
			fewidth = atoi(optarg);
			break;

		case 'h':
			feheight = atoi(optarg);
			break;

		default:
			fprintf(stderr,
				"Usage: %s [-f frontend] [-b bpp] [-w width] [-h height] <url>\n",
				argv[0]);
			return false;
		}
	}

	if (optind < argc) {
		feurl = argv[optind];
	}

	if (nsfb_type_from_name(fename) == NSFB_SURFACE_NONE) {
		if (strcmp(fename, "?") != 0) {
			fprintf(stderr,
				"%s: Unknown surface `%s`\n", argv[0], fename);
		}
		fprintf(stderr, "%s: Valid surface names are:\n", argv[0]);
		nsfb_enumerate_surface_types(framebuffer_surface_iterator, argv[0]);
		return false;
	}

	return true;
}

/**
 * Set option defaults for framebuffer frontend
 *
 * @param defaults The option table to update.
 * @return error status.
 */
static nserror set_defaults(struct nsoption_s *defaults)
{
	int idx;
	static const struct {
		enum nsoption_e nsc;
		colour c;
	} sys_colour_defaults[]= {
		{ NSOPTION_sys_colour_AccentColor, 0x00666666},
		{ NSOPTION_sys_colour_AccentColorText, 0x00ffffff},
		{ NSOPTION_sys_colour_ActiveText, 0x000000ee},
		{ NSOPTION_sys_colour_ButtonBorder, 0x00aaaaaa},
		{ NSOPTION_sys_colour_ButtonFace, 0x00dddddd},
		{ NSOPTION_sys_colour_ButtonText, 0x00000000},
		{ NSOPTION_sys_colour_Canvas, 0x00aaaaaa},
		{ NSOPTION_sys_colour_CanvasText, 0x00000000},
		{ NSOPTION_sys_colour_Field, 0x00f1f1f1},
		{ NSOPTION_sys_colour_FieldText, 0x00000000},
		{ NSOPTION_sys_colour_GrayText, 0x00777777},
		{ NSOPTION_sys_colour_Highlight, 0x00ee0000},
		{ NSOPTION_sys_colour_HighlightText, 0x00000000},
		{ NSOPTION_sys_colour_LinkText, 0x00ee0000},
		{ NSOPTION_sys_colour_Mark, 0x0000ffff},
		{ NSOPTION_sys_colour_MarkText, 0x00000000},
		{ NSOPTION_sys_colour_SelectedItem, 0x00e48435},
		{ NSOPTION_sys_colour_SelectedItemText, 0x00ffffff},
		{ NSOPTION_sys_colour_VisitedText, 0x008b1a55},
		{ NSOPTION_LISTEND, 0},
	};

	/* Set defaults for absent option strings */
#ifdef GEKKO
	/* Leave enough MEM2 for the framebuffer, page DOM, and image decoder.
	 * Choices can still opt back into individual features on real hardware. */
	defaults[NSOPTION_memory_cache_size].value.i = 6 * 1024 * 1024;
	defaults[NSOPTION_disc_cache_size].value.u = 16 * 1024 * 1024;
	defaults[NSOPTION_enable_javascript].value.b = false;
	defaults[NSOPTION_script_timeout].value.i = 3;
	defaults[NSOPTION_block_advertisements].value.b = true;
	defaults[NSOPTION_do_not_track].value.b = true;
	defaults[NSOPTION_background_images].value.b = false;
	defaults[NSOPTION_animate_images].value.b = false;
	defaults[NSOPTION_incremental_reflow].value.b = false;
	defaults[NSOPTION_max_fetchers].value.i = 4;
	defaults[NSOPTION_max_fetchers_per_host].value.i = 2;
	defaults[NSOPTION_max_cached_fetch_handles].value.i = 1;
	defaults[NSOPTION_fb_font_cachesize].value.i = 512;
	nsoption_setnull_charp(disc_cache_path,
			       strdup("sd:/apps/netsurf/Cache"));
	nsoption_setnull_charp(ca_bundle,
		strdup("sd:/apps/netsurf/cacert.pem"));
	nsoption_setnull_charp(cookie_file,
		strdup("sd:/apps/netsurf/Cookies"));
	nsoption_setnull_charp(cookie_jar,
		strdup("sd:/apps/netsurf/Cookies"));
#else
	nsoption_setnull_charp(cookie_file, strdup("~/.netsurf/Cookies"));
	nsoption_setnull_charp(cookie_jar, strdup("~/.netsurf/Cookies"));
#endif

	if (nsoption_charp(cookie_file) == NULL ||
	    nsoption_charp(cookie_jar) == NULL) {
		NSLOG(netsurf, INFO, "Failed initialising cookie options");
		return NSERROR_BAD_PARAMETER;
	}

	/* set system colours for framebuffer ui */
	for (idx=0; sys_colour_defaults[idx].nsc != NSOPTION_LISTEND; idx++) {
		defaults[sys_colour_defaults[idx].nsc].value.c = sys_colour_defaults[idx].c;
	}
	return NSERROR_OK;
}


/**
 * Ensures output logging stream is correctly configured
 */
static bool nslog_stream_configure(FILE *fptr)
{
        /* set log stream to be non-buffering */
	setbuf(fptr, NULL);

	return true;
}

static void framebuffer_run(void)
{
	nsfb_event_t event;
	int timeout; /* timeout in miliseconds */

	while (fb_complete != true) {
#ifdef GEKKO
		if (wii_agent_poll()) {
			fb_complete = true;
			break;
		}
#endif
		/* run the scheduler and discover how long to wait for
		 * the next event.
		 */
		timeout = schedule_run();
#ifdef NETSURF_HBC_AGENT
		if (timeout < 0 || timeout > 250)
			timeout = 250;
#endif

		/* if redraws are pending do not wait for event,
		 * return immediately
		 */
		if (fbtk_get_redraw_pending(fbtk))
			timeout = 0;

		if (fbtk_event(fbtk, &event, timeout)) {
			if ((event.type == NSFB_EVENT_CONTROL) &&
			    (event.value.controlcode ==  NSFB_CONTROL_QUIT))
				fb_complete = true;
		}

#ifdef GEKKO
		bool profile_redraw = fbtk_get_redraw_pending(fbtk);
		if (profile_redraw)
			wii_present_redraw(true);
#endif
		fbtk_redraw(fbtk);
#ifdef GEKKO
		if (profile_redraw)
			wii_present_redraw(false);
#endif
		framebuffer_present();
	}
}

static void gui_quit(void)
{
	NSLOG(netsurf, INFO, "gui_quit");

	urldb_save_cookies(nsoption_charp(cookie_jar));


#ifdef GEKKO
	wii_agent_stage("video shutdown");
#endif
	framebuffer_finalise();
}

/* called back when click in browser window */
static int
fb_browser_window_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	browser_mouse_state mouse;
	int x = cbi->x + bwidget->scrollx;
	int y = cbi->y + bwidget->scrolly;
	uint64_t time_now;
	static struct {
		enum { CLICK_SINGLE, CLICK_DOUBLE, CLICK_TRIPLE } type;
		uint64_t time;
	} last_click;

	if (cbi->event->type != NSFB_EVENT_KEY_DOWN &&
	    cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	NSLOG(netsurf, DEEPDEBUG, "browser window clicked at %d,%d",
			cbi->x, cbi->y);

	switch (cbi->event->type) {
	case NSFB_EVENT_KEY_DOWN:
		switch (cbi->event->value.keycode) {
		case NSFB_KEY_MOUSE_1:
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_PRESS_1, x, y);
			gui_drag.state = GUI_DRAG_PRESSED;
			gui_drag.button = 1;
			gui_drag.x = x;
			gui_drag.y = y;
			break;

		case NSFB_KEY_MOUSE_3:
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_PRESS_2, x, y);
			gui_drag.state = GUI_DRAG_PRESSED;
			gui_drag.button = 2;
			gui_drag.x = x;
			gui_drag.y = y;
			break;

		case NSFB_KEY_MOUSE_4:
			/* scroll up */
			if (browser_window_scroll_at_point(gw->bw,
							   x, y,
							   0, -100) == false)
				widget_scroll_y(gw, -100, false);
			break;

		case NSFB_KEY_MOUSE_5:
			/* scroll down */
			if (browser_window_scroll_at_point(gw->bw,
							   x, y,
							   0, 100) == false)
				widget_scroll_y(gw, 100, false);
			break;

		default:
			break;

		}

		break;
	case NSFB_EVENT_KEY_UP:

		mouse = 0;
		nsu_getmonotonic_ms(&time_now);

		switch (cbi->event->value.keycode) {
		case NSFB_KEY_MOUSE_1:
			if (gui_drag.state == GUI_DRAG_DRAG) {
				/* End of a drag, rather than click */

				if (gui_drag.grabbed_pointer) {
					/* need to ungrab pointer */
					fbtk_tgrab_pointer(widget);
					gui_drag.grabbed_pointer = false;
				}

				gui_drag.state = GUI_DRAG_NONE;

				/* Tell core */
				browser_window_mouse_track(gw->bw, 0, x, y);
				break;
			}
			/* This is a click;
			 * clear PRESSED state and pass to core */
			gui_drag.state = GUI_DRAG_NONE;
			mouse = BROWSER_MOUSE_CLICK_1;
			break;

		case NSFB_KEY_MOUSE_3:
			if (gui_drag.state == GUI_DRAG_DRAG) {
				/* End of a drag, rather than click */
				gui_drag.state = GUI_DRAG_NONE;

				if (gui_drag.grabbed_pointer) {
					/* need to ungrab pointer */
					fbtk_tgrab_pointer(widget);
					gui_drag.grabbed_pointer = false;
				}

				/* Tell core */
				browser_window_mouse_track(gw->bw, 0, x, y);
				break;
			}
			/* This is a click;
			 * clear PRESSED state and pass to core */
			gui_drag.state = GUI_DRAG_NONE;
			mouse = BROWSER_MOUSE_CLICK_2;
			break;

		default:
			break;

		}

		/* Determine if it's a double or triple click, allowing
		 * 0.5 seconds (500ms) between clicks
		 */
		if ((time_now < (last_click.time + 500)) &&
		    (cbi->event->value.keycode != NSFB_KEY_MOUSE_4) &&
		    (cbi->event->value.keycode != NSFB_KEY_MOUSE_5)) {
			if (last_click.type == CLICK_SINGLE) {
				/* Set double click */
				mouse |= BROWSER_MOUSE_DOUBLE_CLICK;
				last_click.type = CLICK_DOUBLE;

			} else if (last_click.type == CLICK_DOUBLE) {
				/* Set triple click */
				mouse |= BROWSER_MOUSE_TRIPLE_CLICK;
				last_click.type = CLICK_TRIPLE;
			} else {
				/* Set normal click */
				last_click.type = CLICK_SINGLE;
			}
		} else {
			last_click.type = CLICK_SINGLE;
		}

		if (mouse) {
			browser_window_mouse_click(gw->bw, mouse, x, y);
		}

		last_click.time = time_now;

		break;
	default:
		break;

	}
	return 1;
}

/* called back when movement in browser window */
static int
fb_browser_window_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	browser_mouse_state mouse = 0;
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	int x = cbi->x + bwidget->scrollx;
	int y = cbi->y + bwidget->scrolly;

	if (gui_drag.state == GUI_DRAG_PRESSED &&
			(abs(x - gui_drag.x) > 5 ||
			 abs(y - gui_drag.y) > 5)) {
		/* Drag started */
		if (gui_drag.button == 1) {
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_DRAG_1,
					gui_drag.x, gui_drag.y);
		} else {
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_DRAG_2,
					gui_drag.x, gui_drag.y);
		}
		gui_drag.grabbed_pointer = fbtk_tgrab_pointer(widget);
		gui_drag.state = GUI_DRAG_DRAG;
	}

	if (gui_drag.state == GUI_DRAG_DRAG) {
		/* set up mouse state */
		mouse |= BROWSER_MOUSE_DRAG_ON;

		if (gui_drag.button == 1)
			mouse |= BROWSER_MOUSE_HOLDING_1;
		else
			mouse |= BROWSER_MOUSE_HOLDING_2;
	}

	browser_window_mouse_track(gw->bw, mouse, x, y);

	return 0;
}

#if defined(GEKKO) && defined(WITH_PDF_EXPORT)
static bool fb_export_pdf(struct gui_window *gw, const char *output_path)
{
	struct hlcache_handle *content;
	struct print_settings *print_settings;

	content = browser_window_get_content(gw->bw);
	if (content == NULL) {
		WII_LOG("PDF export skipped: no page content\n");
		return false;
	}

	haru_nsfont_set_scale((float)nsoption_int(export_scale) / 100);
	print_settings = print_make_settings(PRINT_OPTIONS, output_path,
			&haru_nsfont);
	if (print_settings == NULL) {
		WII_LOG("PDF export failed: no memory\n");
		return false;
	}

	if (print_basic_run(content, &pdf_printer, print_settings) &&
	    pdf_get_save_result() == NSERROR_OK) {
		WII_LOG("PDF exported to %s\n", output_path);
		fbtk_set_text(gw->status, output_path);
		return true;
	} else {
		WII_LOG("PDF export failed\n");
		fbtk_set_text(gw->status, "Unable to save PDF; check SD space");
		return false;
	}
}
#endif

#ifdef GEKKO
/* Explicit developer mode exercises the real browser/export/download paths. */
static bool wii_test_js, wii_test_js_ok, wii_test_filter_ok;
static bool wii_test_mode, wii_test_pdf_ok, wii_test_cache_ok,
	wii_test_cursor_ok, wii_test_gx_ok;
static unsigned int wii_test_rounds, wii_test_phase;
static bool wii_sites_mode;
static unsigned wii_site_seconds = 25;
static unsigned wii_site_min_seconds = 2;
static bool wii_site_cosmetic = true, wii_site_background;
static char wii_site_status[256];

/* Explicit read-only homepage survey. Each page gets a bounded load interval;
 * captures come from the actual presenter rather than the CPU shadow. */
static void wii_site_field(FILE *report, const char *key, const char *value)
{
	fprintf(report, "%s=", key);
	if (value)
		for (; *value; value++)
			fputc(*value == '\n' || *value == '\r' ? ' ' : *value,
			      report);
	fputc('\n', report);
}

/* Stream diagnostic text without allocating a second copy of the entire DOM. */
static void wii_site_dom_text(FILE *report, dom_node *node, size_t *remaining, unsigned depth)
{
	dom_node_type type;
	dom_node *child = NULL, *next;
	dom_string *text = NULL;

	if (*remaining == 0 || depth == 128)
		return;
	if (dom_node_get_node_type(node, &type) == DOM_NO_ERR &&
	    (type == DOM_TEXT_NODE || type == DOM_CDATA_SECTION_NODE) &&
	    dom_node_get_node_value(node, &text) == DOM_NO_ERR && text) {
		size_t length = dom_string_byte_length(text);
		if (length > *remaining)
			length = *remaining;
		*remaining -= fwrite(dom_string_data(text), 1, length, report);
		dom_string_unref(text);
	}
	if (dom_node_get_first_child(node, &child) != DOM_NO_ERR)
		return;
	while (child) {
		wii_site_dom_text(report, child, remaining, depth + 1);
		next = NULL;
		if (*remaining != 0)
			dom_node_get_next_sibling(child, &next);
		dom_node_unref(child);
		child = next;
	}
}

/* Read the renderer's visible text, independently of the live DOM capture. */
static void wii_site_layout_text(FILE *report, struct box *root)
{
	struct box *box = root;
	size_t remaining = 2 * 1024 * 1024;
	while (box != NULL && remaining > 1) {
		if (box->text && box_visible(box)) {
			size_t length = box->length < remaining - 1 ? box->length : remaining - 1;
			fwrite(box->text, 1, length, report);
			fputc('\n', report);
			remaining -= length + 1;
		}
		if (box->children) { box = box->children; continue; }
		while (box != root && !box->next) box = box->parent;
		box = box == root ? NULL : box->next;
	}
}

static void wii_sites_poll(void *context)
{
	static FILE *sites;
	static unsigned index, phase;
	static unsigned network_rounds;
	static uint64_t started;
	static char requested[512];
	static bool scrolled;
	struct gui_window *gw = window_list;
	struct hlcache_handle *content;
	uint64_t now = 0;
	char path[128];
	(void)context;
	if (!gw) {
		fb_complete = true;
		return;
	}
	content = browser_window_get_content(gw->bw);
	nsu_getmonotonic_ms(&now);
	if (index == 0 && wiisocket_get_status() != 1) {
		if (++network_rounds > 120) {
			WII_LOG("site survey: network startup did not complete\n");
			fb_complete = true;
		} else
			framebuffer_schedule(500, wii_sites_poll, NULL);
		return;
	}
	if (phase == 0) {
		nsurl *url;
		if (!sites) {
			mkdir("sd:/apps/netsurf/site-results", 0777);
			sites = fopen("sd:/apps/netsurf/wii-sites.txt", "r");
		}
		if (!sites || !fgets(requested, sizeof(requested), sites)) {
			FILE *report = fopen(
				"sd:/apps/netsurf/site-results/complete.txt",
				"w");
			if (report) {
				fprintf(report,
					"sites=%u\njavascript=%u\n",
					index,
					nsoption_bool(enable_javascript));
				request_filter_report(report);
				fclose(report);
			}
			if (sites)
				fclose(sites);
			fb_complete = true;
			return;
		}
		requested[strcspn(requested, "\r\n")] = 0;
		if ((strncmp(requested, "https://", 8) != 0 &&
		     strncmp(requested, "file:///sd:/apps/netsurf/", 24) !=
			     0) ||
		    nsurl_create(requested, &url) != NSERROR_OK) {
			fclose(sites);
			fb_complete = true;
			return;
		}
		index++;
		wii_site_status[0] = 0;
		started = now;
		WII_LOG("site %u opening %s\n", index, requested);
		browser_window_navigate(gw->bw,
					url,
					NULL,
					BW_NAVIGATE_HISTORY,
					NULL,
					NULL,
					NULL);
		nsurl_unref(url);
		phase = 1;
	} else if (phase == 1) {
		/* about:blank separates sites so a failed fetch cannot be
		 * mistaken for the previous site's successfully loaded content.
		 */
		bool page = content &&
			    strcmp(nsurl_access(
					   hlcache_handle_get_url(content)),
				   "about:blank") != 0;
		bool done = page &&
			    content_get_status(content) == CONTENT_STATUS_DONE;
		if (now - started >= (uint64_t)wii_site_seconds * 1000 ||
		    (now - started >= (uint64_t)wii_site_min_seconds * 1000 &&
		     gw->throbber_index < 0)) {
			nsurl *url = NULL;
			FILE *report;
			snprintf(path,
				 sizeof(path),
				 "sd:/apps/netsurf/site-results/%02u.txt",
				 index);
			report = fopen(path, "w");
			if (report) {
				wii_site_field(report, "requested", requested);
				if (browser_window_get_url(
					    gw->bw, true, &url) == NSERROR_OK) {
					wii_site_field(report,
						       "final",
						       nsurl_access(url));
					nsurl_unref(url);
				}
				wii_site_field(report,
					       "title",
					       content ? content_get_title(
								 content)
						       : "");
				wii_site_field(report,
					       "status",
					       wii_site_status);
				fprintf(report,
					"done=%u\npage=%u\nelapsed_ms=%llu\nwidth=%d\nheight=%d\n",
					done,
					page,
					(unsigned long long)(now - started),
					content ? content_get_width(content)
						: 0,
					content ? content_get_height(content)
						: 0);
				fclose(report);
			}
			if (content) {
				size_t length = 0;
				const uint8_t *source = content_get_source_data(
					content, &length);
				snprintf(
					path,
					sizeof(path),
					"sd:/apps/netsurf/site-results/%02u-source.html",
					index);
				report = fopen(path, "wb");
				if (report) {
					if (length > 2 * 1024 * 1024)
						length = 2 * 1024 * 1024;
					if (source)
						fwrite(source, 1, length, report);
					fclose(report);
				}
			}
			/* Preserve results inserted by scripts; source omits DOM changes. */
			snprintf(path, sizeof(path),
				 "sd:/apps/netsurf/site-results/%02u-dom.txt", index);
			report = fopen(path, "wb");
			if (report) {
				if (content && content_get_type(content) == CONTENT_HTML) {
					dom_node *root = NULL;
					dom_document *document = html_get_document(content);
					size_t remaining = 2 * 1024 * 1024;
					if (document)
						dom_document_get_document_element(document, (void *)&root);
					if (root)
						wii_site_dom_text(report, root, &remaining, 0);
					dom_node_unref(root);
				}
				fclose(report);
			}
			snprintf(path, sizeof(path),
				 "sd:/apps/netsurf/site-results/%02u-layout.txt", index);
			report = fopen(path, "wb");
			if (report) {
				if (content && content_get_type(content) == CONTENT_HTML)
					wii_site_layout_text(report, html_get_box_tree(content));
				fclose(report);
			}
			browser_window_stop(gw->bw);
			phase = 2;
		}
	} else if (phase == 2) {
		int w, h;
		framebuffer_present();
		snprintf(path,
			 sizeof(path),
			 "sd:/apps/netsurf/site-results/%02u-top.ppm",
			 index);
		wii_present_capture(path);
		browser_window_get_extents(gw->bw, true, &w, &h);
		scrolled = h > fbtk_get_height(gw->browser);
		if (scrolled)
			widget_scroll_y(gw,
					fbtk_get_height(gw->browser),
					false);
		phase = 3;
	} else {
		nsurl *blank;
		FILE *report;
		framebuffer_present();
		snprintf(path,
			 sizeof(path),
			 "sd:/apps/netsurf/site-results/%02u-scroll.ppm",
			 index);
		wii_present_capture(path);
		snprintf(path,
			 sizeof(path),
			 "sd:/apps/netsurf/site-results/%02u.txt",
			 index);
		report = fopen(path, "a");
		if (report) {
			fprintf(report, "scroll_requested=%u\n", scrolled);
			fclose(report);
		}
		if (nsurl_create("about:blank", &blank) == NSERROR_OK) {
			browser_window_navigate(gw->bw,
						blank,
						NULL,
						BW_NAVIGATE_HISTORY,
						NULL,
						NULL,
						NULL);
			nsurl_unref(blank);
		}
		phase = 0;
	}
	framebuffer_schedule(phase == 2 || phase == 3 ? 1000 : 500,
			     wii_sites_poll,
			     NULL);
}

/* Exercise the real cache boundary; a blocked probe must never start a fetch.
 */
static nserror wii_filter_probe_callback(llcache_handle *handle,
					 const llcache_event *event,
					 void *pw)
{
	(void)handle;
	(void)event;
	(void)pw;
	return NSERROR_OK;
}

static bool wii_test_request_filter(void)
{
	nsurl *url = NULL, *referer = NULL;
	llcache_handle *handle = NULL;
	nserror result = NSERROR_NOMEM;
	if (nsurl_create("https://wii-probe.doubleclick.net/pixel", &url) ==
		    NSERROR_OK &&
	    nsurl_create("file:///sd:/apps/netsurf/wii-test.html", &referer) ==
		    NSERROR_OK)
		result = llcache_handle_retrieve(url,
						 0,
						 referer,
						 NULL,
						 wii_filter_probe_callback,
						 NULL,
						 &handle);
	if (handle != NULL) {
		llcache_handle_abort(handle);
		llcache_handle_release(handle);
	}
	if (url != NULL)
		nsurl_unref(url);
	if (referer != NULL)
		nsurl_unref(referer);
	return result == NSERROR_PERMISSION;
}

static bool wii_test_cache(void)
{
	nsurl *url;
	uint8_t *data = (uint8_t *)strdup("Wii SD cache regression");
	uint8_t *loaded = NULL;
	size_t length = 0;
	bool valid = false;
	if (data == NULL)
		return false;
	if (nsurl_create("https://netsurf.invalid/wii-smoke-cache", &url) !=
	    NSERROR_OK) {
		free(data);
		return false;
	}
	if (filesystem_llcache_table->store(
		    url, BACKING_STORE_NONE, data, 23) == NSERROR_OK) {
		filesystem_llcache_table->release(url, BACKING_STORE_NONE);
		if (filesystem_llcache_table->fetch(
			    url, BACKING_STORE_NONE, &loaded, &length) ==
		    NSERROR_OK) {
			valid = length == 23 &&
				memcmp(loaded, "Wii SD cache regression", 23) ==
					0;
			filesystem_llcache_table->release(url,
							  BACKING_STORE_NONE);
		}
	} else {
		/* The store may have taken ownership before its write failed.
		 */
		filesystem_llcache_table->release(url, BACKING_STORE_NONE);
	}
	filesystem_llcache_table->invalidate(url);
	nsurl_unref(url);
	return valid;
}

static void wii_test_poll(void *context)
{
	struct gui_window *gw = window_list;
	struct hlcache_handle *content = gw == NULL
						 ? NULL
						 : browser_window_get_content(
							   gw->bw);
	FILE *report;
	(void)context;
	if (++wii_test_rounds > 300) {
		report = fopen("sd:/apps/netsurf/wii-test.txt", "w");
		if (report != NULL) {
			fprintf(report,
				"FAIL: browser smoke timeout phase=%u\n",
				wii_test_phase);
			fclose(report);
		}
		fb_complete = true;
		return;
	}
	/* The fixture finishes asynchronous DOM/image checks after load. */
	if (wii_test_phase == 0 && content != NULL &&
	    content_get_status(content) == CONTENT_STATUS_DONE &&
	    (!wii_test_js || strcmp(content_get_title(content),
				  "NetSurf Wii JavaScript PASS") == 0)) {
		wii_test_js_ok = !wii_test_js ||
				 strcmp(content_get_title(content),
					"NetSurf Wii JavaScript PASS") == 0;
		wii_test_phase = 1;
		framebuffer_schedule(1000, wii_test_poll, NULL);
		return;
	}
	if (wii_test_phase == 1) {
		nsfb_t *surface = fbtk_get_nsfb(fbtk);
		framebuffer_present();
		wii_present_profile_checkpoint();
		wii_test_gx_ok = wii_present_test_gx() &&
				 framebuffer_gx_test_offscreen();
		wii_test_cursor_ok = wii_present_test_cursor();
		wii_present_capture("sd:/apps/netsurf/wii-test-gx.ppm");
		unsigned char *pixels;
		int width, height, stride, x, y;
		nsurl *download_url;
		FILE *capture = fopen("sd:/apps/netsurf/wii-test.ppm", "wb");
		nsfb_get_geometry(surface, &width, &height, NULL);
		nsfb_get_buffer(surface, &pixels, &stride);
		if (capture != NULL) {
			fprintf(capture, "P6\n%d %d\n255\n", width, height);
			for (y = 0; y < height; y++)
				for (x = 0; x < width; x++) {
					unsigned char rgb[3];
					if (febpp == 16) {
						const unsigned char *p =
							pixels + y * stride +
							x * 2;
						unsigned color = (p[0] << 8) |
								 p[1];
						rgb[0] = ((color >> 11) & 31) *
							 255 / 31;
						rgb[1] = ((color >> 5) & 63) *
							 255 / 63;
						rgb[2] = (color & 31) * 255 /
							 31;
					} else {
						memcpy(rgb,
						       pixels + y * stride +
							       x * 4 + 1,
						       3);
					}
					fwrite(rgb, 1, 3, capture);
				}
			fclose(capture);
		}
		report = fopen("sd:/apps/netsurf/wii-test.txt", "w");
		if (report != NULL) {
			fputs("phase=PDF export\n", report);
			fclose(report);
		}
#ifdef WITH_PDF_EXPORT
		wii_test_pdf_ok = fb_export_pdf(
			gw, "sd:/apps/netsurf/wii-test.pdf");
#endif
		wii_test_cache_ok = wii_test_cache();
		if (request_filter_active())
			wii_test_filter_ok = wii_test_request_filter();
		wii_log_memory("smoke rendered");
		if (nsurl_create("file:///sd:/apps/netsurf/wii-test.bin",
				 &download_url) == NSERROR_OK) {
			browser_window_navigate(gw->bw,
						download_url,
						NULL,
						BW_NAVIGATE_DOWNLOAD,
						NULL,
						NULL,
						NULL);
			nsurl_unref(download_url);
		}
		wii_test_phase = 2;
		framebuffer_schedule(2000, wii_test_poll, NULL);
		return;
	}
	if (wii_test_phase == 2) {
		char payload[20] = {0};
		FILE *download = fopen(
			"sd:/apps/netsurf/Downloads/wii-test.bin", "rb");
		bool valid = download != NULL &&
			     fread(payload, 1, 16, download) == 16 &&
			     memcmp(payload, "netsurf-wii-test", 16) == 0;
		if (download != NULL)
			fclose(download);
		report = fopen("sd:/apps/netsurf/wii-test.txt", "w");
		if (report != NULL) {
			fprintf(report,
				"page=PASS\npdf=%s\ndownload=%s\ncache=%s\n",
#ifdef WITH_PDF_EXPORT
				wii_test_pdf_ok ? "PASS" : "FAIL",
#else
				"DISABLED",
#endif
				valid ? "PASS" : "FAIL",
				wii_test_cache_ok ? "PASS" : "FAIL");
			if (wii_test_js)
				fprintf(report,
					"javascript=%s\n",
					wii_test_js_ok ? "PASS" : "FAIL");
			fprintf(report,
				"renderer=%s\ndepth=%d\n",
				framebuffer_renderer_name(),
				febpp);
			wii_present_stats(report);
			request_filter_report(report);
			if (request_filter_active())
				fprintf(report,
					"request_filter_test=%s\n",
					wii_test_filter_ok ? "PASS" : "FAIL");
			fprintf(report,
				"gx_contract=%s\n",
				wii_test_gx_ok ? "PASS" : "FAIL");
			{
				struct mallinfo heap = mallinfo();
				fprintf(report,
					"cursor=%s\nheap_used=%u\nheap_free=%u\n",
					wii_test_cursor_ok ? "PASS" : "FAIL",
					wii_heap_used(),
					(unsigned)heap.fordblks);
			}
			fprintf(report,
				"MEM1_remaining=%u\nMEM2_remaining=%u\nMEM2_high=%p\n",
				(unsigned)((uintptr_t)SYS_GetArena1Hi() -
					   (uintptr_t)SYS_GetArena1Lo()),
				(unsigned)((uintptr_t)SYS_GetArena2Hi() -
					   (uintptr_t)SYS_GetArena2Lo()),
				SYS_GetArena2Hi());
			fclose(report);
		}
		fb_complete = true;
		return;
	}
	framebuffer_schedule(100, wii_test_poll, NULL);
}
#endif


static int
fb_browser_window_input(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	static fbtk_modifier_type modifier = FBTK_MOD_CLEAR;
	int ucs4 = -1;

	NSLOG(netsurf, INFO, "got value %d", cbi->event->value.keycode);

	switch (cbi->event->type) {
	case NSFB_EVENT_KEY_DOWN:
		switch (cbi->event->value.keycode) {

		case NSFB_KEY_DELETE:
			browser_window_key_press(gw->bw, NS_KEY_DELETE_RIGHT);
			break;

		case NSFB_KEY_PAGEUP:
			if (browser_window_key_press(gw->bw,
					NS_KEY_PAGE_UP) == false)
				widget_scroll_y(gw, -fbtk_get_height(
						gw->browser), false);
			break;

		case NSFB_KEY_PAGEDOWN:
			if (browser_window_key_press(gw->bw,
					NS_KEY_PAGE_DOWN) == false)
				widget_scroll_y(gw, fbtk_get_height(
						gw->browser), false);
			break;

		case NSFB_KEY_RIGHT:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				/* CTRL held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_LINE_END) == false)
					widget_scroll_x(gw, INT_MAX, true);

			} else if (modifier & FBTK_MOD_RSHIFT ||
					modifier & FBTK_MOD_LSHIFT) {
				/* SHIFT held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_WORD_RIGHT) == false)
					widget_scroll_x(gw, fbtk_get_width(
						gw->browser), false);

			} else {
				/* no modifier */
				if (browser_window_key_press(gw->bw,
						NS_KEY_RIGHT) == false)
					widget_scroll_x(gw, 100, false);
			}
			break;

		case NSFB_KEY_LEFT:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				/* CTRL held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_LINE_START) == false)
					widget_scroll_x(gw, 0, true);

			} else if (modifier & FBTK_MOD_RSHIFT ||
					modifier & FBTK_MOD_LSHIFT) {
				/* SHIFT held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_WORD_LEFT) == false)
					widget_scroll_x(gw, -fbtk_get_width(
						gw->browser), false);

			} else {
				/* no modifier */
				if (browser_window_key_press(gw->bw,
						NS_KEY_LEFT) == false)
					widget_scroll_x(gw, -100, false);
			}
			break;

		case NSFB_KEY_UP:
			if (browser_window_key_press(gw->bw,
					NS_KEY_UP) == false)
				widget_scroll_y(gw, -100, false);
			break;

		case NSFB_KEY_DOWN:
			if (browser_window_key_press(gw->bw,
					NS_KEY_DOWN) == false)
				widget_scroll_y(gw, 100, false);
			break;

		case NSFB_KEY_MINUS:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				browser_window_set_scale(gw->bw, -0.1, false);
			}
			break;

		case NSFB_KEY_EQUALS: /* PLUS */
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				browser_window_set_scale(gw->bw, 0.1, false);
			}
			break;

		case NSFB_KEY_0:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				browser_window_set_scale(gw->bw, 1.0, true);
			}
			break;

		case NSFB_KEY_RSHIFT:
			modifier |= FBTK_MOD_RSHIFT;
			break;

		case NSFB_KEY_LSHIFT:
			modifier |= FBTK_MOD_LSHIFT;
			break;

		case NSFB_KEY_RCTRL:
			modifier |= FBTK_MOD_RCTRL;
			break;

		case NSFB_KEY_LCTRL:
			modifier |= FBTK_MOD_LCTRL;
			break;

#if defined(GEKKO) && defined(WITH_PDF_EXPORT)
		case NSFB_KEY_p:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				fb_export_pdf(gw,
					      "sd:/apps/netsurf/netsurf.pdf");
				break;
			}
			fallthrough;
#endif

		case NSFB_KEY_y:
		case NSFB_KEY_z:
			if (cbi->event->value.keycode == NSFB_KEY_z &&
					(modifier & FBTK_MOD_RCTRL ||
					 modifier & FBTK_MOD_LCTRL) &&
					(modifier & FBTK_MOD_RSHIFT ||
					 modifier & FBTK_MOD_LSHIFT)) {
				/* Z pressed with CTRL and SHIFT held */
				browser_window_key_press(gw->bw, NS_KEY_REDO);
				break;

			} else if (cbi->event->value.keycode == NSFB_KEY_z &&
					(modifier & FBTK_MOD_RCTRL ||
					 modifier & FBTK_MOD_LCTRL)) {
				/* Z pressed with CTRL held */
				browser_window_key_press(gw->bw, NS_KEY_UNDO);
				break;

			} else if (cbi->event->value.keycode == NSFB_KEY_y &&
					(modifier & FBTK_MOD_RCTRL ||
					 modifier & FBTK_MOD_LCTRL)) {
				/* Y pressed with CTRL held */
				browser_window_key_press(gw->bw, NS_KEY_REDO);
				break;
			}
			/* Z or Y pressed but not undo or redo; */
			fallthrough;

		default:
			ucs4 = fbtk_keycode_to_ucs4(cbi->event->value.keycode,
						    modifier);
			if (ucs4 != -1)
				browser_window_key_press(gw->bw, ucs4);
			break;
		}
		break;

	case NSFB_EVENT_KEY_UP:
		switch (cbi->event->value.keycode) {
		case NSFB_KEY_RSHIFT:
			modifier &= ~FBTK_MOD_RSHIFT;
			break;

		case NSFB_KEY_LSHIFT:
			modifier &= ~FBTK_MOD_LSHIFT;
			break;

		case NSFB_KEY_RCTRL:
			modifier &= ~FBTK_MOD_RCTRL;
			break;

		case NSFB_KEY_LCTRL:
			modifier &= ~FBTK_MOD_LCTRL;
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}

	return 0;
}

static void
fb_update_back_forward(struct gui_window *gw)
{
	struct browser_window *bw = gw->bw;

	fbtk_set_bitmap(gw->back,
			(browser_window_back_available(bw)) ?
			&left_arrow : &left_arrow_g);
	fbtk_set_bitmap(gw->forward,
			(browser_window_forward_available(bw)) ?
			&right_arrow : &right_arrow_g);
}

/* left icon click routine */
static int
fb_leftarrow_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_window *bw = gw->bw;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	if (browser_window_back_available(bw))
		browser_window_history_back(bw, false);

	fb_update_back_forward(gw);

	return 1;
}

/* right arrow icon click routine */
static int
fb_rightarrow_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_window *bw = gw->bw;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	if (browser_window_forward_available(bw))
		browser_window_history_forward(bw, false);

	fb_update_back_forward(gw);
	return 1;

}

/* reload icon click routine */
static int
fb_reload_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	browser_window_reload(bw, true);
	return 1;
}

/* stop icon click routine */
static int
fb_stop_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	browser_window_stop(bw);
	return 0;
}

static int
fb_osk_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	map_osk();

	return 0;
}

/* close browser window icon click routine */
static int
fb_close_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	fb_complete = true;

	return 0;
}

static int
fb_scroll_callback(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;

	switch (cbi->type) {
	case FBTK_CBT_SCROLLY:
		widget_scroll_y(gw, cbi->y, true);
		break;

	case FBTK_CBT_SCROLLX:
		widget_scroll_x(gw, cbi->x, true);
		break;

	default:
		break;
	}
	return 0;
}

static int
fb_url_enter(void *pw, char *text)
{
	struct browser_window *bw = pw;
	nsurl *url;
	nserror error;

	error = nsurl_create(text, &url);
	if (error != NSERROR_OK) {
		fb_warn_user("Errorcode:", messages_get_errorcode(error));
	} else {
		browser_window_navigate(bw, url, NULL, BW_NAVIGATE_HISTORY,
				NULL, NULL, NULL);
		nsurl_unref(url);
	}

	return 0;
}

static int
fb_url_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	framebuffer_set_cursor(&caret_image);
	return 0;
}

static int
set_ptr_default_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	framebuffer_set_cursor(&pointer_image);
	return 0;
}

static int
fb_localhistory_btn_clik(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	fb_local_history_present(fbtk, gw->bw);

	return 0;
}


/** Create a toolbar window and populate it with buttons. 
 *
 * The toolbar layout uses a character to define buttons type and position:
 * b - back
 * l - local history
 * f - forward
 * s - stop 
 * r - refresh
 * u - url bar expands to fit remaining space
 * t - throbber/activity indicator
 * c - close the current window
 *
 * The default layout is "blfsrut" there should be no more than a
 * single url bar entry or behaviour will be undefined.
 *
 * @param gw Parent window 
 * @param toolbar_height The height in pixels of the toolbar
 * @param padding The padding in pixels round each element of the toolbar
 * @param frame_col Frame colour.
 * @param toolbar_layout A string defining which buttons and controls
 *                       should be added to the toolbar. May be empty
 *                       string to disable the bar..
 * 
 */
static fbtk_widget_t *
create_toolbar(struct gui_window *gw, 
	       int toolbar_height, 
	       int padding, 
	       colour frame_col,
	       const char *toolbar_layout)
{
	fbtk_widget_t *toolbar;
	fbtk_widget_t *widget;

	int xpos; /* The position of the next widget. */
	int xlhs = 0; /* extent of the left hand side widgets */
	int xdir = 1; /* the direction of movement + or - 1 */
	const char *itmtype; /* type of the next item */

	if (toolbar_layout == NULL) {
		toolbar_layout = NSFB_TOOLBAR_DEFAULT_LAYOUT;
	}

	NSLOG(netsurf, INFO, "Using toolbar layout %s", toolbar_layout);

	itmtype = toolbar_layout;

	/* check for the toolbar being disabled */
	if ((*itmtype == 0) || (*itmtype == 'q')) {
		return NULL;
	}

	toolbar = fbtk_create_window(gw->window, 0, 0, 0, 
				     toolbar_height, 
				     frame_col);

	if (toolbar == NULL) {
		return NULL;
	}

	fbtk_set_handler(toolbar, 
			 FBTK_CBT_POINTERENTER, 
			 set_ptr_default_move, 
			 NULL);


	xpos = padding;

	/* loop proceeds creating widget on the left hand side until
	 * it runs out of layout or encounters a url bar declaration
	 * wherupon it works backwards from the end of the layout
	 * untill the space left is for the url bar
	 */
	while ((itmtype >= toolbar_layout) && 
	       (*itmtype != 0) && 
	       (xdir !=0)) {

		NSLOG(netsurf, INFO, "toolbar adding %c", *itmtype);


		switch (*itmtype) {

		case 'b': /* back */
			widget = fbtk_create_button(toolbar, 
						    (xdir == 1) ? xpos : 
						     xpos - left_arrow.width, 
						    padding, 
						    left_arrow.width, 
						    -padding, 
						    frame_col, 
						    &left_arrow, 
						    fb_leftarrow_click, 
						    gw);
			gw->back = widget; /* keep reference */
			break;

		case 'l': /* local history */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1) ? xpos : 
						     xpos - history_image.width,
						    padding,
						    history_image.width,
						    -padding,
						    frame_col,
						    &history_image,
						    fb_localhistory_btn_clik,
						    gw);
			gw->history = widget;
			break;

		case 'f': /* forward */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - right_arrow.width,
						    padding,
						    right_arrow.width,
						    -padding,
						    frame_col,
						    &right_arrow,
						    fb_rightarrow_click,
						    gw);
			gw->forward = widget;
			break;

		case 'c': /* close the current window */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - stop_image_g.width,
						    padding,
						    stop_image_g.width,
						    -padding,
						    frame_col,
						    &stop_image_g,
						    fb_close_click,
						    gw->bw);
			gw->close = widget;
			break;

		case 's': /* stop  */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - stop_image.width,
						    padding,
						    stop_image.width,
						    -padding,
						    frame_col,
						    &stop_image,
						    fb_stop_click,
						    gw->bw);
			gw->stop = widget;
			break;

		case 'r': /* reload */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - reload.width,
						    padding,
						    reload.width,
						    -padding,
						    frame_col,
						    &reload,
						    fb_reload_click,
						    gw->bw);
			gw->reload = widget;
			break;

		case 't': /* throbber/activity indicator */
			widget = fbtk_create_bitmap(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - throbber0.width,
						    padding,
						    throbber0.width,
						    -padding,
						    frame_col, 
						    &throbber0);
			gw->throbber = widget;
			break;


		case 'u': /* url bar*/
			if (xdir == -1) {
				/* met the u going backwards add url
				 * now we know available extent 
				 */ 

				widget = fbtk_create_writable_text(toolbar,
						   xlhs,
						   padding,
						   xpos - xlhs,
						   -padding,
						   FB_COLOUR_WHITE,
						   FB_COLOUR_BLACK,
						   true,
						   fb_url_enter,
						   gw->bw);

				fbtk_set_handler(widget, 
						 FBTK_CBT_POINTERENTER, 
						 fb_url_move, gw->bw);

				gw->url = widget; /* keep reference */

				/* toolbar is complete */
				xdir = 0;
				break;
			}
			/* met url going forwards, note position and
			 * reverse direction 
			 */
			itmtype = toolbar_layout + strlen(toolbar_layout);
			xdir = -1;
			xlhs = xpos;
			xpos = (2 * fbtk_get_width(toolbar));
			widget = toolbar;
			break;

		default:
			widget = NULL;
			xdir = 0;
			NSLOG(netsurf, INFO,
			      "Unknown element %c in toolbar layout",
			      *itmtype);
		        break;

		}

		if (widget != NULL) {
			xpos += (xdir * (fbtk_get_width(widget) + padding));
		}

		NSLOG(netsurf, INFO, "xpos is %d", xpos);

		itmtype += xdir;
	}

	fbtk_set_mapping(toolbar, true);

	return toolbar;
}


/** Resize a toolbar.
 *
 * @param gw Parent window
 * @param toolbar_height The height in pixels of the toolbar
 * @param padding The padding in pixels round each element of the toolbar
 * @param toolbar_layout A string defining which buttons and controls
 *                       should be added to the toolbar. May be empty
 *                       string to disable the bar.
 */
static void
resize_toolbar(struct gui_window *gw,
	       int toolbar_height,
	       int padding,
	       const char *toolbar_layout)
{
	fbtk_widget_t *widget;

	int xpos; /* The position of the next widget. */
	int xlhs = 0; /* extent of the left hand side widgets */
	int xdir = 1; /* the direction of movement + or - 1 */
	const char *itmtype; /* type of the next item */
	int x = 0, y = 0, w = 0, h = 0;

	if (gw->toolbar == NULL) {
		return;
	}

	if (toolbar_layout == NULL) {
		toolbar_layout = NSFB_TOOLBAR_DEFAULT_LAYOUT;
	}

	itmtype = toolbar_layout;

	if (*itmtype == 0) {
		return;
	}

	fbtk_set_pos_and_size(gw->toolbar, 0, 0, 0, toolbar_height);

	xpos = padding;

	/* loop proceeds creating widget on the left hand side until
	 * it runs out of layout or encounters a url bar declaration
	 * wherupon it works backwards from the end of the layout
	 * untill the space left is for the url bar
	 */
	while (itmtype >= toolbar_layout && xdir != 0) {

		switch (*itmtype) {
		case 'b': /* back */
			widget = gw->back;
			x = (xdir == 1) ? xpos : xpos - left_arrow.width;
			y = padding;
			w = left_arrow.width;
			h = -padding;
			break;

		case 'l': /* local history */
			widget = gw->history;
			x = (xdir == 1) ? xpos : xpos - history_image.width;
			y = padding;
			w = history_image.width;
			h = -padding;
			break;

		case 'f': /* forward */
			widget = gw->forward;
			x = (xdir == 1) ? xpos : xpos - right_arrow.width;
			y = padding;
			w = right_arrow.width;
			h = -padding;
			break;

		case 'c': /* close the current window */
			widget = gw->close;
			x = (xdir == 1) ? xpos : xpos - stop_image_g.width;
			y = padding;
			w = stop_image_g.width;
			h = -padding;
			break;

		case 's': /* stop  */
			widget = gw->stop;
			x = (xdir == 1) ? xpos : xpos - stop_image.width;
			y = padding;
			w = stop_image.width;
			h = -padding;
			break;

		case 'r': /* reload */
			widget = gw->reload;
			x = (xdir == 1) ? xpos : xpos - reload.width;
			y = padding;
			w = reload.width;
			h = -padding;
			break;

		case 't': /* throbber/activity indicator */
			widget = gw->throbber;
			x = (xdir == 1) ? xpos : xpos - throbber0.width;
			y = padding;
			w = throbber0.width;
			h = -padding;
			break;


		case 'u': /* url bar*/
			if (xdir == -1) {
				/* met the u going backwards add url
				 * now we know available extent
				 */
				widget = gw->url;
				x = xlhs;
				y = padding;
				w = xpos - xlhs;
				h = -padding;

				/* toolbar is complete */
				xdir = 0;
				break;
			}
			/* met url going forwards, note position and
			 * reverse direction
			 */
			itmtype = toolbar_layout + strlen(toolbar_layout);
			xdir = -1;
			xlhs = xpos;
			w = fbtk_get_width(gw->toolbar);
			xpos = 2 * w;
			widget = gw->toolbar;
			break;

		default:
			widget = NULL;
		        break;

		}

		if (widget != NULL) {
			if (widget != gw->toolbar)
				fbtk_set_pos_and_size(widget, x, y, w, h);
			xpos += xdir * (w + padding);
		}

		itmtype += xdir;
	}
}

/** Routine called when "stripped of focus" event occours for browser widget.
 *
 * @param widget The widget reciving "stripped of focus" event.
 * @param cbi The callback parameters.
 * @return The callback result.
 */
static int
fb_browser_window_strip_focus(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	fbtk_set_caret(widget, false, 0, 0, 0, NULL);

	return 0;
}

static void
create_browser_widget(struct gui_window *gw, int toolbar_height, int furniture_width)
{
	struct browser_widget_s *browser_widget;
	browser_widget = calloc(1, sizeof(struct browser_widget_s));

	gw->browser = fbtk_create_user(gw->window,
				       0,
				       toolbar_height,
				       -furniture_width,
				       -furniture_width,
				       browser_widget);

	fbtk_set_handler(gw->browser, FBTK_CBT_REDRAW, fb_browser_window_redraw, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_DESTROY, fb_browser_window_destroy, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_INPUT, fb_browser_window_input, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_CLICK, fb_browser_window_click, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_STRIP_FOCUS, fb_browser_window_strip_focus, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_POINTERMOVE, fb_browser_window_move, gw);
}

static void
resize_browser_widget(struct gui_window *gw, int x, int y,
		int width, int height)
{
	fbtk_set_pos_and_size(gw->browser, x, y, width, height);
	browser_window_schedule_reformat(gw->bw);
}

static void
create_normal_browser_window(struct gui_window *gw, int furniture_width)
{
	fbtk_widget_t *widget;
	fbtk_widget_t *toolbar;
	int statusbar_width = 0;
	int toolbar_height = nsoption_int(fb_toolbar_size);

	NSLOG(netsurf, INFO, "Normal window");

	gw->window = fbtk_create_window(fbtk, 0, 0, 0, 0, 0);

	statusbar_width = nsoption_int(toolbar_status_size) *
		fbtk_get_width(gw->window) / 10000;

	/* toolbar */
	toolbar = create_toolbar(gw, 
				 toolbar_height, 
				 2, 
				 FB_FRAME_COLOUR, 
				 nsoption_charp(fb_toolbar_layout));
	gw->toolbar = toolbar;

	/* set the actually created toolbar height */
	if (toolbar != NULL) {
		toolbar_height = fbtk_get_height(toolbar);
	} else {
		toolbar_height = 0;
	}

	/* status bar */
	gw->status = fbtk_create_text(gw->window,
				      0,
				      fbtk_get_height(gw->window) - furniture_width,
				      statusbar_width, furniture_width,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
	fbtk_set_handler(gw->status, FBTK_CBT_POINTERENTER, set_ptr_default_move, NULL);

	NSLOG(netsurf, INFO, "status bar %p at %d,%d", gw->status,
	      fbtk_get_absx(gw->status), fbtk_get_absy(gw->status));

	/* create horizontal scrollbar */
	gw->hscroll = fbtk_create_hscroll(gw->window,
					  statusbar_width,
					  fbtk_get_height(gw->window) - furniture_width,
					  fbtk_get_width(gw->window) - statusbar_width - furniture_width,
					  furniture_width,
					  FB_SCROLL_COLOUR,
					  FB_FRAME_COLOUR,
					  fb_scroll_callback,
					  gw);

	/* fill bottom right area */

	if (nsoption_bool(fb_osk) == true) {
		widget = fbtk_create_text_button(gw->window,
						 fbtk_get_width(gw->window) - furniture_width,
						 fbtk_get_height(gw->window) - furniture_width,
						 furniture_width,
						 furniture_width,
						 FB_FRAME_COLOUR, FB_COLOUR_BLACK,
						 fb_osk_click,
						 NULL);
		widget = fbtk_create_button(gw->window,
				fbtk_get_width(gw->window) - furniture_width,
				fbtk_get_height(gw->window) - furniture_width,
				furniture_width,
				furniture_width,
				FB_FRAME_COLOUR,
				&osk_image,
				fb_osk_click,
				NULL);
	} else {
		widget = fbtk_create_fill(gw->window,
					  fbtk_get_width(gw->window) - furniture_width,
					  fbtk_get_height(gw->window) - furniture_width,
					  furniture_width,
					  furniture_width,
					  FB_FRAME_COLOUR);

		fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, set_ptr_default_move, NULL);
	}

	gw->bottom_right = widget;

	/* create vertical scrollbar */
	gw->vscroll = fbtk_create_vscroll(gw->window,
					  fbtk_get_width(gw->window) - furniture_width,
					  toolbar_height,
					  furniture_width,
					  fbtk_get_height(gw->window) - toolbar_height - furniture_width,
					  FB_SCROLL_COLOUR,
					  FB_FRAME_COLOUR,
					  fb_scroll_callback,
					  gw);

	/* browser widget */
	create_browser_widget(gw, toolbar_height, nsoption_int(fb_furniture_size));

	/* Give browser_window's user widget input focus */
	fbtk_set_focus(gw->browser);
}

static void
resize_normal_browser_window(struct gui_window *gw, int furniture_width)
{
	bool resized;
	int width, height;
	int statusbar_width;
	int toolbar_height = fbtk_get_height(gw->toolbar);

	/* Resize the main window widget */
	resized = fbtk_set_pos_and_size(gw->window, 0, 0, 0, 0);
	if (!resized)
		return;

	width = fbtk_get_width(gw->window);
	height = fbtk_get_height(gw->window);
	statusbar_width = nsoption_int(toolbar_status_size) * width / 10000;

	resize_toolbar(gw, toolbar_height, 2,
			nsoption_charp(fb_toolbar_layout));
	fbtk_set_pos_and_size(gw->status,
			0, height - furniture_width,
			statusbar_width, furniture_width);
	fbtk_reposition_hscroll(gw->hscroll,
			statusbar_width, height - furniture_width,
			width - statusbar_width - furniture_width,
			furniture_width);
	fbtk_set_pos_and_size(gw->bottom_right,
			width - furniture_width, height - furniture_width,
			furniture_width, furniture_width);
	fbtk_reposition_vscroll(gw->vscroll,
			width - furniture_width,
			toolbar_height, furniture_width,
			height - toolbar_height - furniture_width);
	resize_browser_widget(gw,
			0, toolbar_height,
			width - furniture_width,
			height - furniture_width - toolbar_height);
}

static void gui_window_add_to_window_list(struct gui_window *gw)
{
	gw->next = NULL;
	gw->prev = NULL;

	if (window_list == NULL) {
		window_list = gw;
	} else {
		window_list->prev = gw;
		gw->next = window_list;
		window_list = gw;
	}
}

static void gui_window_remove_from_window_list(struct gui_window *gw)
{
	struct gui_window *list;

	for (list = window_list; list != NULL; list = list->next) {
		if (list != gw)
			continue;

		if (list == window_list) {
			window_list = list->next;
			if (window_list != NULL)
				window_list->prev = NULL;
		} else {
			list->prev->next = list->next;
			if (list->next != NULL) {
				list->next->prev = list->prev;
			}
		}
		break;
	}
}


static struct gui_window *
gui_window_create(struct browser_window *bw,
		struct gui_window *existing,
		gui_window_create_flags flags)
{
	struct gui_window *gw;

	gw = calloc(1, sizeof(struct gui_window));

	if (gw == NULL)
		return NULL;

	/* associate the gui window with the underlying browser window
	 */
	gw->bw = bw;

	create_normal_browser_window(gw, nsoption_int(fb_furniture_size));

	/* map and request redraw of gui window */
	fbtk_set_mapping(gw->window, true);

	/* Add it to the window list */
	gui_window_add_to_window_list(gw);

	return gw;
}

static void
gui_window_destroy(struct gui_window *gw)
{
	gui_window_remove_from_window_list(gw);

	fbtk_destroy_widget(gw->window);

	free(gw);
}


/**
 * Invalidates an area of a framebuffer browser window
 *
 * \param g The netsurf window being invalidated.
 * \param rect area to redraw or NULL for the entire window area
 * \return NSERROR_OK on success or appropriate error code
 */
static nserror
fb_window_invalidate_area(struct gui_window *g, const struct rect *rect)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);

	if (rect != NULL) {
		fb_queue_redraw(g->browser,
				rect->x0 - bwidget->scrollx,
				rect->y0 - bwidget->scrolly,
				rect->x1 - bwidget->scrollx,
				rect->y1 - bwidget->scrolly);
	} else {
		fb_queue_redraw(g->browser,
				0,
				0,
				fbtk_get_width(g->browser),
				fbtk_get_height(g->browser));
	}
	return NSERROR_OK;
}

static bool
gui_window_get_scroll(struct gui_window *g, int *sx, int *sy)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);

	*sx = bwidget->scrollx;
	*sy = bwidget->scrolly;

	return true;
}

/**
 * Set the scroll position of a framebuffer browser window.
 *
 * Scrolls the viewport to ensure the specified rectangle of the
 *   content is shown. The framebuffer implementation scrolls the contents so
 *   the specified point in the content is at the top of the viewport.
 *
 * \param gw gui_window to scroll
 * \param rect The rectangle to ensure is shown.
 * \return NSERROR_OK on success or apropriate error code.
 */
static nserror
gui_window_set_scroll(struct gui_window *gw, const struct rect *rect)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);

	assert(bwidget);

	widget_scroll_x(gw, rect->x0, true);
	widget_scroll_y(gw, rect->y0, true);

	return NSERROR_OK;
}


/**
 * Find the current dimensions of a framebuffer browser window content area.
 *
 * \param gw The gui window to measure content area of.
 * \param width receives width of window
 * \param height receives height of window
 * \return NSERROR_OK on sucess and width and height updated.
 */
static nserror
gui_window_get_dimensions(struct gui_window *gw, int *width, int *height)
{
	*width = fbtk_get_width(gw->browser);
	*height = fbtk_get_height(gw->browser);

	return NSERROR_OK;
}

static void
gui_window_update_extent(struct gui_window *gw)
{
	int w, h;
	browser_window_get_extents(gw->bw, true, &w, &h);

	fbtk_set_scroll_parameters(gw->hscroll, 0, w,
			fbtk_get_width(gw->browser), 100);

	fbtk_set_scroll_parameters(gw->vscroll, 0, h,
			fbtk_get_height(gw->browser), 100);
}

static void
gui_window_set_status(struct gui_window *g, const char *text)
{
#ifdef GEKKO
	if (wii_sites_mode && text)
		snprintf(wii_site_status, sizeof(wii_site_status), "%s", text);
#endif
	fbtk_set_text(g->status, text);
}

static void
gui_window_set_pointer(struct gui_window *g, gui_pointer_shape shape)
{
	switch (shape) {
	case GUI_POINTER_POINT:
		framebuffer_set_cursor(&hand_image);
		break;

	case GUI_POINTER_CARET:
		framebuffer_set_cursor(&caret_image);
		break;

	case GUI_POINTER_MENU:
		framebuffer_set_cursor(&menu_image);
		break;

	case GUI_POINTER_PROGRESS:
		framebuffer_set_cursor(&progress_image);
		break;

	case GUI_POINTER_MOVE:
		framebuffer_set_cursor(&move_image);
		break;

	default:
		framebuffer_set_cursor(&pointer_image);
		break;
	}
}

static nserror
gui_window_set_url(struct gui_window *g, nsurl *url)
{
	const char *display_url = nsurl_access(url);

#ifdef GEKKO
	/* A colon in a file URL path is normally percent-encoded. Wii device
	 * paths use it as the volume separator, so show sd:/ as users enter it. */
	if (strncasecmp(display_url, "file:///sd%3A/", 14) == 0) {
		char *wii_url = malloc(strlen(display_url) - 1);
		if (wii_url != NULL) {
			memcpy(wii_url, display_url, 10);
			wii_url[10] = ':';
			strcpy(wii_url + 11, display_url + 13);
			fbtk_set_text(g->url, wii_url);
			free(wii_url);
			return NSERROR_OK;
		}
	}
#endif

	fbtk_set_text(g->url, display_url);
	return NSERROR_OK;
}

static void
throbber_advance(void *pw)
{
	struct gui_window *g = pw;
	struct fbtk_bitmap *image;

	switch (g->throbber_index) {
	case 0:
		image = &throbber1;
		g->throbber_index = 1;
		break;

	case 1:
		image = &throbber2;
		g->throbber_index = 2;
		break;

	case 2:
		image = &throbber3;
		g->throbber_index = 3;
		break;

	case 3:
		image = &throbber4;
		g->throbber_index = 4;
		break;

	case 4:
		image = &throbber5;
		g->throbber_index = 5;
		break;

	case 5:
		image = &throbber6;
		g->throbber_index = 6;
		break;

	case 6:
		image = &throbber7;
		g->throbber_index = 7;
		break;

	case 7:
		image = &throbber8;
		g->throbber_index = 0;
		break;

	default:
		return;
	}

	if (g->throbber_index >= 0) {
		fbtk_set_bitmap(g->throbber, image);
		framebuffer_schedule(100, throbber_advance, g);
	}
}

static void
gui_window_start_throbber(struct gui_window *g)
{
	g->throbber_index = 0;
	framebuffer_schedule(100, throbber_advance, g);
}

static void
gui_window_stop_throbber(struct gui_window *gw)
{
	gw->throbber_index = -1;
	fbtk_set_bitmap(gw->throbber, &throbber0);

	fb_update_back_forward(gw);

}

static void
gui_window_remove_caret_cb(fbtk_widget_t *widget)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	int c_x, c_y, c_h;

	if (fbtk_get_caret(widget, &c_x, &c_y, &c_h)) {
		/* browser window already had caret:
		 * redraw its area to remove it first */
		fb_queue_redraw(widget,
				c_x - bwidget->scrollx,
				c_y - bwidget->scrolly,
				c_x + 1 - bwidget->scrollx,
				c_y + c_h - bwidget->scrolly);
	}
}

static void
gui_window_place_caret(struct gui_window *g, int x, int y, int height,
		const struct rect *clip)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);

	/* set new pos */
	fbtk_set_caret(g->browser, true, x, y, height,
			gui_window_remove_caret_cb);

	/* redraw new caret pos */
	fb_queue_redraw(g->browser,
			x - bwidget->scrollx,
			y - bwidget->scrolly,
			x + 1 - bwidget->scrollx,
			y + height - bwidget->scrolly);
}

static void
gui_window_remove_caret(struct gui_window *g)
{
	int c_x, c_y, c_h;

	if (fbtk_get_caret(g->browser, &c_x, &c_y, &c_h)) {
		/* browser window owns the caret, so can remove it */
		fbtk_set_caret(g->browser, false, 0, 0, 0, NULL);
	}
}

/**
 * process miscellaneous window events
 *
 * \param gw The window receiving the event.
 * \param event The event code.
 * \return NSERROR_OK when processed ok
 */
static nserror
gui_window_event(struct gui_window *gw, enum gui_window_event event)
{
	switch (event) {
	case GW_EVENT_UPDATE_EXTENT:
		gui_window_update_extent(gw);
		break;

	case GW_EVENT_REMOVE_CARET:
		gui_window_remove_caret(gw);
		break;

	case GW_EVENT_START_THROBBER:
		gui_window_start_throbber(gw);
		break;

	case GW_EVENT_STOP_THROBBER:
		gui_window_stop_throbber(gw);
		break;

	default:
		break;
	}
	return NSERROR_OK;
}

#ifdef GEKKO
static void gui_window_console_log(struct gui_window *gw,
		browser_window_console_source source, const char *message,
		size_t length, browser_window_console_flags flags);
#endif

static struct gui_window_table framebuffer_window_table = {
	.create = gui_window_create,
	.destroy = gui_window_destroy,
	.invalidate = fb_window_invalidate_area,
	.get_scroll = gui_window_get_scroll,
	.set_scroll = gui_window_set_scroll,
	.get_dimensions = gui_window_get_dimensions,
	.event = gui_window_event,

	.set_url = gui_window_set_url,
	.set_status = gui_window_set_status,
	.set_pointer = gui_window_set_pointer,
	.place_caret = gui_window_place_caret,
#ifdef GEKKO
	.console_log = gui_window_console_log,
#endif
};


static nserror gui_get_screen_dimensions(int *width, int *height, int *depth)
{
	enum nsfb_format_e format;
	if (fbtk == NULL || nsfb_get_geometry(fbtk_get_nsfb(fbtk), width, height, &format) != 0)
		return NSERROR_INIT_FAILED;
	/* Packed color bits, without alpha or padding. */
	switch (format) {
	case NSFB_FMT_ARGB1555: *depth = 15; break;
	case NSFB_FMT_RGB565: *depth = 16; break;
	case NSFB_FMT_I8: *depth = 8; break;
	case NSFB_FMT_I4: *depth = 4; break;
	case NSFB_FMT_I1: *depth = 1; break;
	default: *depth = 24; break;
	}
	return NSERROR_OK;
}

#ifdef GEKKO
static void gui_window_console_log(struct gui_window *gw,
		browser_window_console_source source, const char *message,
		size_t length, browser_window_console_flags flags)
{
	(void)gw;
	(void)flags;
	/* Keep caught exceptions visible in agent builds and opt-in surveys. */
#ifndef NETSURF_HBC_AGENT
	if (!wii_sites_mode)
		return;
#endif
	if (source == BW_CS_SCRIPT_CONSOLE)
		NSLOG(jserrors, WARNING, "Page console: %.*s",
		      (int)(length > 4096 ? 4096 : length), message);
}
#endif

static struct gui_misc_table framebuffer_misc_table = {
	.schedule = framebuffer_schedule,
	.get_screen_dimensions = gui_get_screen_dimensions,

	.quit = gui_quit,
};

#ifdef GEKKO
static void wii_network_started(int result, void *context)
{
	(void)context;
	WII_LOG("network startup complete (%d)\n", result);
	wii_agent_network_ready(result);
}
#endif

/**
 * Entry point from OS.
 *
 * /param argc The number of arguments in the string vector.
 * /param argv The argument string vector.
 * /return The return code to the OS
 */
int
main(int argc, char** argv)
{
	struct browser_window *bw;
	char *options;
	char *messages;
	nsurl *url;
	nserror ret;
	nsfb_t *nsfb;
	struct netsurf_table framebuffer_table = {
		.misc = &framebuffer_misc_table,
		.window = &framebuffer_window_table,
		.corewindow = framebuffer_core_window_table,
		.clipboard = framebuffer_clipboard_table,
		.fetch = framebuffer_fetch_table,
		.utf8 = framebuffer_utf8_table,
		.bitmap = framebuffer_bitmap_table,
		.layout = framebuffer_layout_table,
	};

#ifdef GEKKO
	{
		int index;
		for (index = 1; index < argc; index++) {
			if (strcmp(argv[index], "--wii-test") == 0) {
				wii_test_mode = true;
				memmove(argv + index,
					argv + index + 1,
					(argc - index) * sizeof(*argv));
				argc--;
				break;
			}
		}
	}
	SYS_STDIO_Report(true);
	WII_LOG("entry\n");
	if (!fatInitDefault())
		fprintf(stderr, "Unable to mount SD/USB storage\n");
	WII_LOG("FAT initialised\n");
	wii_agent_stage("startup");
	{
		FILE *test = fopen("sd:/apps/netsurf/wii-test.cfg", "r");
		if (test != NULL) {
			char setting[32] = {0};
			if (fgets(setting, sizeof(setting), test) != NULL &&
			    strcmp(setting, "selftest=1\n") == 0)
				wii_test_mode = true;
			while (fgets(setting, sizeof(setting), test) != NULL) {
				if (strcmp(setting, "agent-crash=1\n") == 0)
					wii_agent_arm_crash();
				else if (strcmp(setting, "exit-stall=1\n") == 0)
					wii_agent_arm_shutdown_test();
				else if (strcmp(setting, "javascript=1\n") == 0)
					wii_test_js = true;
				else if (strcmp(setting, "sites=1\n") == 0)
					wii_sites_mode = true;
				else if (strcmp(setting, "cosmetic=0\n") == 0)
					wii_site_cosmetic = false;
				else if (strcmp(setting, "background=1\n") == 0)
					wii_site_background = true;
				else if (strncmp(setting,
						 "site-seconds=",
						 13) == 0) {
					unsigned seconds;
					if (sscanf(setting + 13,
						   "%u",
						   &seconds) == 1 &&
					    seconds >= 5 && seconds <= 90)
						wii_site_seconds = seconds;
				} else if (strncmp(setting, "site-min-seconds=", 17) == 0) {
					unsigned seconds;
					if (sscanf(setting + 17, "%u", &seconds) == 1 &&
					    seconds >= 2 && seconds <= 90)
						wii_site_min_seconds = seconds;
				} else if (strcmp(setting, "mem2test=1\n") == 0)
					wii_agent_arm_memory_test();
			}
			fclose(test);
		}
	}
	{
		int network_result = wiisocket_async_init(wii_network_started,
							  NULL);
		if (network_result < 0)
			fprintf(stderr, "Unable to start Wii networking\n");
		else if (network_result == 1)
			wii_agent_network_ready(
				0); /* already initialized: no callback */
	}
	wii_log_memory("startup");
#endif

#ifdef GEKKO
	framebuffer_table.file = wii_get_file_table();
	framebuffer_table.download = &wii_download_table;
	framebuffer_table.llcache = filesystem_llcache_table;
#endif

        ret = netsurf_register(&framebuffer_table);
        if (ret != NSERROR_OK) {
		die("NetSurf operation table failed registration");
        }
	WII_LOG("frontend registered\n");

	respaths = fb_init_resource_path(NETSURF_FB_RESPATH":"NETSURF_FB_FONTPATH);
	WII_LOG("resource paths initialised\n");

	/* initialise logging. Not fatal if it fails but not much we
	 * can do about it either.
	 */
#ifdef GEKKO
	if (wii_sites_mode) {
		mkdir("sd:/apps/netsurf/site-results", 0777);
		int log_argc = 3;
		char log_name[] = "netsurf", log_flag[] = "-V";
		char log_path[] = "sd:/apps/netsurf/site-results/browser.log";
		char *log_argv[] = {log_name, log_flag, log_path, NULL};
		nslog_init(nslog_stream_configure, &log_argc, log_argv);
	} else
#endif
		nslog_init(nslog_stream_configure, &argc, argv);

	/* user options setup */
	ret = nsoption_init(set_defaults, &nsoptions, &nsoptions_default);
	if (ret != NSERROR_OK) {
		die("Options failed to initialise");
	}
	WII_LOG("options initialised\n");
	options = filepath_find(respaths, "Choices");
	nsoption_read(options, nsoptions);
	free(options);
	nsoption_commandline(&argc, argv, nsoptions);
#ifdef GEKKO
	if (wii_test_mode) {
		nsoption_set_bool(background_images, true);
		nsoption_set_bool(enable_javascript, wii_test_js);
		nsoption_set_charp(
			homepage_url,
			strdup("file:///sd:/apps/netsurf/wii-test.html"));
	}
	if (wii_sites_mode) {
		nsoption_set_bool(enable_javascript, wii_test_js);
		nsoption_set_bool(block_advertisements, wii_site_cosmetic);
		nsoption_set_bool(background_images, wii_site_background);
		nsoption_set_charp(homepage_url, strdup("about:blank"));
	}
	if (nsoption_charp(fb_request_filter) &&
	    strcmp(nsoption_charp(fb_request_filter), "hosts") == 0 &&
	    !request_filter_init("sd:/apps/netsurf/adblock-hosts.txt",
				 "sd:/apps/netsurf/adblock-allow.txt"))
		WII_LOG("hostname filter disabled: missing, invalid or oversized policy\n");
#endif

	/* message init */
	messages = filepath_find(respaths, "Messages");
        ret = messages_add_from_file(messages);
	free(messages);
	if (ret != NSERROR_OK) {
		fprintf(stderr, "Message translations failed to load\n");
	}

	/* common initialisation */
	ret = netsurf_init(NULL);
	if (ret != NSERROR_OK) {
		die("NetSurf failed to initialise");
	}
	WII_LOG("core initialised\n");

	/* Override, since we have no support for non-core SELECT menu */
	nsoption_set_bool(core_select_menu, true);

	if (process_cmdline(argc,argv) != true)
		die("unable to process command line.\n");

	nsfb = framebuffer_initialise(fename, fewidth, feheight, febpp);
	if (nsfb == NULL)
		die("Unable to initialise framebuffer");
	WII_LOG("framebuffer initialised (%s %dx%dx%d)\n",
		fename, fewidth, feheight, febpp);

	framebuffer_set_cursor(&pointer_image);

	if (fb_font_init() == false)
		die("Unable to initialise the font system");

	fbtk = fbtk_init(nsfb);
	WII_LOG("toolkit initialised\n");
#ifdef GEKKO
	wii_log_memory("browser ready");
#endif

#ifdef GEKKO
	/* Real CRTs commonly overscan a 640x480 picture, cropping a margin
	 * from every edge that nothing in VIDEO/GX compensates for on its
	 * own. Shrinking the root widget's bounds here -- rather than the
	 * underlying nsfb surface -- keeps every child window (browser
	 * content, toolbar, on-screen keyboard, both sized off
	 * fbtk_get_width/height(root)) and the pointer's clamped range
	 * (fbtk_warp_pointer() clamps to root->x/y/width/height) inside the
	 * TV-visible picture, without touching plotting code. */
	{
		int full_width = fbtk_get_width(fbtk);
		int full_height = fbtk_get_height(fbtk);
		int margin_x = full_width * 5 / 100;
		int margin_y = full_height * 5 / 100;
		fbtk_set_pos_and_size(fbtk, margin_x, margin_y,
				full_width - (2 * margin_x),
				full_height - (2 * margin_y));
	}
#endif

	fbtk_enable_oskb(fbtk);

	urldb_load_cookies(nsoption_charp(cookie_file));

#ifdef NETSURF_HBC_AGENT
	wii_agent_stage("network readiness");
	for (unsigned attempt = 0; attempt < 500 && wiisocket_get_status() != 1;
	     attempt++)
		usleep(100000);
	if (wiisocket_get_status() != 1) {
		wii_agent_stage("network startup timeout");
		return EXIT_FAILURE;
	}
	wii_agent_network_ready(0);
	wii_agent_poll();
	wii_agent_stage("browser navigation");
#endif

	/* create an initial browser window */

	NSLOG(netsurf, INFO, "calling browser_window_create");

	ret = nsurl_create(feurl, &url);
	WII_LOG("opening %s\n", feurl);
	if (ret == NSERROR_OK) {
		ret = browser_window_create(BW_CREATE_HISTORY,
					      url,
					      NULL,
					      NULL,
					      &bw);
		nsurl_unref(url);
	}
	if (ret != NSERROR_OK) {
		fb_warn_user("Errorcode:", messages_get_errorcode(ret));
	} else {
#ifdef GEKKO
		if (wii_test_mode)
			framebuffer_schedule(100, wii_test_poll, NULL);
		else if (wii_sites_mode)
			framebuffer_schedule(1000, wii_sites_poll, NULL);
#endif
#ifdef GEKKO
		wii_agent_stage("event loop");
#endif
		framebuffer_run();
#ifdef GEKKO
		wii_agent_begin_shutdown();
		wii_agent_stage("destroy browser");
#endif

		browser_window_destroy(bw);
	}

#ifdef GEKKO
	wii_agent_stage("core cleanup");
#endif
	netsurf_exit();
#ifdef GEKKO
	request_filter_finalise();
#endif

#ifdef GEKKO
	wii_agent_stage("font cleanup");
#endif
	if (fb_font_finalise() == false)
		NSLOG(netsurf, INFO, "Font finalisation failed.");

	/* finalise options */
	nsoption_finalise(nsoptions, nsoptions_default);

	/* finalise logging */
	nslog_finalise();
#ifdef GEKKO
	wii_agent_stage("return to HBC");
#endif

	return 0;
}

void gui_resize(fbtk_widget_t *root, int width, int height)
{
	struct gui_window *gw;
	nsfb_t *nsfb = fbtk_get_nsfb(root);

	/* Enforce a minimum */
	if (width < 300)
		width = 300;
	if (height < 200)
		height = 200;

	if (framebuffer_resize(nsfb, width, height, febpp) == false) {
		return;
	}

	fbtk_set_pos_and_size(root, 0, 0, width, height);

	fewidth = width;
	feheight = height;

	for (gw = window_list; gw != NULL; gw = gw->next) {
		resize_normal_browser_window(gw,
				nsoption_int(fb_furniture_size));
	}

	fbtk_request_redraw(root);
}


/*
 * Local Variables:
 * c-basic-offset:8
 * End:
 */
