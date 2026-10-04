/* SPDX-License-Identifier: GPL-2.0-only */
/* Shared GX presenter and native page rendering, with a separate cursor. */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libnsfb.h>
#include <libnsfb_plot.h>
#include <libnsfb_plot_util.h>
#include <libnsfb_cursor.h>
#include "framebuffer/wii_present.h"
#include "framebuffer/wii_tiles.h"

/* Public SDL-wii hook; stop its GX owner before taking over presentation. */
void OGC_VideoStop(void);
static struct {
	nsfb_t *surface;
	uint8_t *pixels, *texture, *dirty, *stale;
	void *xfb;
	GXRModeObj *mode;
	GXTexObj page, pointer;
	int width, height, pitch, bpp, columns, rows;
	const uint32_t *cursor;
	int cursor_w, cursor_h, cursor_x, cursor_y;
	bool changed, cursor_uploaded;
	unsigned frames, tiles, flushed;
	uint64_t ticks, upload_ticks, sync_ticks, texture_ticks, redraw_ticks,
		redraw_started;
	unsigned redraws, cache_hits, cache_misses;
	uint64_t hash_bytes;
	bool accelerated, pending;
	unsigned stale_count;
	unsigned gpu_rectangles, gpu_images, gpu_glyphs, readbacks;
	struct {
		uintptr_t pc;
		unsigned count;
		uint64_t ticks;
	} sync_sources[16];
} video;
static struct {
	uint64_t redraw, present, upload, sync, texture;
	unsigned frames, redraws, tiles, flushed, readbacks;
} scene;
static uint8_t presentation_fifo[GX_FIFO_MINSIZE] ATTRIBUTE_ALIGN(32);
static uint8_t cursor_texture[64 * 64 * 4] ATTRIBUTE_ALIGN(32);

static void damage(void *context, const nsfb_bbox_t *box)
{
	int x, y;
	int left = box->x0 < 0 ? 0 : box->x0;
	int top = box->y0 < 0 ? 0 : box->y0;
	int right = box->x1 > video.width ? video.width : box->x1;
	int bottom = box->y1 > video.height ? video.height : box->y1;
	(void)context;
	if (left >= right || top >= bottom)
		return;
	if (video.pending || video.stale_count) {
		video.changed = true;
		return;
	}
	for (y = top / 4; y < (bottom + 3) / 4; y++)
		for (x = left / 4; x < (right + 3) / 4; x++)
			video.dirty[y * video.columns + x] = 1;
	video.changed = true;
}

static void cursor(void *context,
		   const uint32_t *pixels,
		   int width,
		   int height,
		   int stride,
		   int x,
		   int y)
{
	int px, py;
	(void)context;
	if (width < 0 || height < 0 || width > 64 || height > 64 ||
	    stride < width)
		return;
	if (pixels != video.cursor || width != video.cursor_w ||
	    height != video.cursor_h) {
		memset(cursor_texture, 0, sizeof(cursor_texture));
		for (py = 0; pixels != NULL && py < height; py++) {
			for (px = 0; px < width; px++) {
				uint32_t color = pixels[py * stride + px];
				size_t offset = ((py / 4) * 16 + px / 4) * 64 +
						((py % 4) * 4 + px % 4) * 2;
				cursor_texture[offset] = color >> 24;
				cursor_texture[offset + 1] = color;
				cursor_texture[offset + 32] = color >> 8;
				cursor_texture[offset + 33] = color >> 16;
			}
		}
		DCStoreRange(cursor_texture, sizeof(cursor_texture));
		video.cursor_uploaded = true;
		video.changed = true;
	}
	if (pixels != video.cursor || x != video.cursor_x ||
	    y != video.cursor_y)
		video.changed = true;
	video.cursor = pixels;
	video.cursor_w = width;
	video.cursor_h = height;
	video.cursor_x = x;
	video.cursor_y = y;
}

static void textured_quad(float x,
			  float y,
			  float width,
			  float height,
			  float u0,
			  float v0,
			  float u,
			  float v)
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	GX_Position3f32(x, y, 0);
	GX_TexCoord2f32(u0, v0);
	GX_Position3f32(x + width, y, 0);
	GX_TexCoord2f32(u, v0);
	GX_Position3f32(x + width, y + height, 0);
	GX_TexCoord2f32(u, v);
	GX_Position3f32(x, y + height, 0);
	GX_TexCoord2f32(u0, v);
	GX_End();
}

static void quad(float x, float y, float width, float height, float u, float v)
{
	textured_quad(x, y, width, height, 0, 0, u, v);
}

bool wii_present_start(nsfb_t *surface)
{
	enum nsfb_format_e format;
	size_t size;
	Mtx model;
	Mtx44 projection;
	nsfb_bbox_t all;
	memset(&video, 0, sizeof(video));
	memset(&scene, 0, sizeof(scene));
	nsfb_get_geometry(surface, &video.width, &video.height, &format);
	if ((format != NSFB_FMT_XRGB8888 && format != NSFB_FMT_RGB565) ||
	    video.width <= 0 || video.height <= 0 || video.width > 1024 ||
	    video.height > 1024 || (video.width % 4) != 0 ||
	    (video.height % 4) != 0)
		return false;
	video.bpp = format == NSFB_FMT_RGB565 ? 16 : 32;
	video.columns = video.width / 4;
	video.rows = video.height / 4;
	size = video.width * video.height * (video.bpp / 8);
	video.texture = memalign(32, size);
	video.dirty = calloc(video.columns * video.rows, 1);
	video.mode = VIDEO_GetPreferredMode(NULL);
	/* Match SDL-wii's PAL substitution so VI and our XFB agree. */
	if (video.mode == &TVPal528IntDf)
		video.mode = &TVPal576IntDfScale;
	video.xfb = SYS_AllocateFramebuffer(video.mode);
	if (video.texture == NULL || video.dirty == NULL || video.xfb == NULL) {
		free(video.texture);
		free(video.dirty);
		free(video.xfb);
		memset(&video, 0, sizeof(video));
		return false;
	}
	video.surface = surface;
	nsfb_get_buffer(surface, &video.pixels, &video.pitch);
	OGC_VideoStop();
	GX_DrawDone();
	GX_Init(presentation_fifo, sizeof(presentation_fifo));
	GX_SetCurrentGXThread();
	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetDither(GX_FALSE);
	GX_SetCopyFilter(video.mode->aa,
			 video.mode->sample_pattern,
			 GX_FALSE,
			 video.mode->vfilter);
	GX_SetFieldMode(video.mode->field_rendering,
			video.mode->viHeight == 2 * video.mode->xfbHeight
				? GX_ENABLE
				: GX_DISABLE);
	GX_SetViewport(0, 0, video.mode->fbWidth, video.mode->efbHeight, 0, 1);
	GX_SetScissor(0, 0, video.mode->fbWidth, video.mode->efbHeight);
	GX_SetDispCopySrc(0, 0, video.mode->fbWidth, video.mode->efbHeight);
	GX_SetDispCopyDst(video.mode->fbWidth, video.mode->xfbHeight);
	GX_SetDispCopyYScale((float)video.mode->xfbHeight /
			     video.mode->efbHeight);
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	guOrtho(projection, 0, video.height, 0, video.width, 100, 1000);
	guMtxIdentity(model);
	guMtxTransApply(model, model, 0, 0, -100);
	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_LoadPosMtxImm(model, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_SetNumChans(0);
	GX_SetNumTexGens(1);
	GX_SetNumTevStages(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
	GX_SetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
	GX_SetColorUpdate(GX_TRUE);
	GX_SetAlphaUpdate(GX_FALSE);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_InitTexObj(&video.page,
		      video.texture,
		      video.width,
		      video.height,
		      video.bpp == 16 ? GX_TF_RGB565 : GX_TF_RGBA8,
		      GX_CLAMP,
		      GX_CLAMP,
		      GX_FALSE);
	GX_InitTexObjLOD(&video.page,
			 GX_NEAR,
			 GX_NEAR,
			 0,
			 0,
			 0,
			 GX_FALSE,
			 GX_FALSE,
			 GX_ANISO_1);
	GX_InitTexObj(&video.pointer,
		      cursor_texture,
		      64,
		      64,
		      GX_TF_RGBA8,
		      GX_CLAMP,
		      GX_CLAMP,
		      GX_FALSE);
	VIDEO_SetNextFramebuffer(MEM_K0_TO_K1(video.xfb));
	VIDEO_Flush();
	nsfb_wii_set_presenter(surface, damage, cursor, NULL);
	all = (nsfb_bbox_t){0, 0, video.width, video.height};
	damage(NULL, &all);
	return true;
}

static void upload_page(void)
{
	uint64_t started = gettime();
	int x, y, bytes;
	bool upload = video.cursor_uploaded;
	bytes = video.bpp == 16 ? 32 : 64;
	for (y = 0; y < video.rows; y++) {
		int first = -1;
		for (x = 0; x <= video.columns; x++) {
			if (x < video.columns &&
			    video.dirty[y * video.columns + x]) {
				size_t offset = (y * video.columns + x) * bytes;
				wii_convert_tile(video.texture + offset,
						 video.pixels +
							 y * 4 * video.pitch +
							 x * 4 * video.bpp / 8,
						 video.pitch,
						 video.bpp);
				video.dirty[y * video.columns + x] = 0;
				if (first < 0)
					first = x;
				video.tiles++;
				upload = true;
			} else if (first >= 0) {
				int length = (x - first) * bytes;
				DCStoreRange(video.texture +
						     (y * video.columns +
						      first) *
							     bytes,
					     length);
				video.flushed += length;
				first = -1;
			}
		}
	}
	if (upload)
		GX_InvalidateTexAll();
	video.upload_ticks += gettime() - started;
}

/* EFB is the drawing target. Save it before presentation adds the cursor. */
static void save_page(void)
{
	if (!video.pending)
		return;
	size_t size = video.width * video.height * video.bpp / 8;
	GX_SetTexCopySrc(0, 0, video.width, video.height);
	GX_SetTexCopyDst(video.width,
			 video.height,
			 video.bpp == 16 ? GX_TF_RGB565 : GX_TF_RGBA8,
			 GX_FALSE);
	GX_CopyTex(video.texture, GX_FALSE);
	GX_PixModeSync();
	GX_DrawDone();
	DCInvalidateRange(video.texture, size);
	GX_InvalidateTexAll();
	video.pending = false;
}

static void cpu_access(void *context, const nsfb_bbox_t *region, bool writing)
{
	uint64_t started = gettime();
	nsfb_bbox_t box = region ? *region
				 : (nsfb_bbox_t){
					   0, 0, video.width, video.height};
	int tx, ty, x, y, bytes = video.bpp == 16 ? 32 : 64;
	bool decoded = false;
	(void)context;
	save_page();
	if (box.x0 < 0)
		box.x0 = 0;
	if (box.y0 < 0)
		box.y0 = 0;
	if (box.x1 > video.width)
		box.x1 = video.width;
	if (box.y1 > video.height)
		box.y1 = video.height;
	for (ty = box.y0 / 4; ty < (box.y1 + 3) / 4; ty++)
		for (tx = box.x0 / 4; tx < (box.x1 + 3) / 4; tx++) {
			int tile = ty * video.columns + tx;
			if (video.stale[tile]) {
				for (y = 0; y < 4; y++)
					for (x = 0; x < 4; x++) {
						size_t offset = tile * bytes +
								(y * 4 + x) * 2;
						uint8_t *pixel =
							video.pixels +
							(ty * 4 + y) *
								video.pitch +
							(tx * 4 + x) *
								video.bpp / 8;
						if (video.bpp == 16) {
							pixel[0] =
								video.texture
									[offset];
							pixel[1] =
								video.texture
									[offset +
									 1];
						} else {
							pixel[0] = 255;
							pixel[1] =
								video.texture
									[offset +
									 1];
							pixel[2] =
								video.texture
									[offset +
									 32];
							pixel[3] =
								video.texture
									[offset +
									 33];
						}
					}
				video.stale[tile] = 0;
				video.stale_count--;
				decoded = true;
			}
			if (writing)
				video.dirty[tile] = 1;
		}
	if (decoded) {
		uintptr_t pc = (uintptr_t)__builtin_return_address(0);
		video.readbacks++;
		for (unsigned i = 0; i < 16; i++) {
			if (video.sync_sources[i].pc == 0 ||
			    video.sync_sources[i].pc == pc) {
				video.sync_sources[i].pc = pc;
				video.sync_sources[i].count++;
				video.sync_sources[i].ticks += gettime() -
							       started;
				break;
			}
		}
	}
	if (writing)
		video.changed = true;
	video.sync_ticks += gettime() - started;
}

/* GPU writes invalidate only the CPU tiles actually touched. Software widgets
 * and fallbacks synchronize their region, rather than reading every page pixel.
 */
static void gpu_damage(const nsfb_bbox_t *clip, const nsfb_bbox_t *box)
{
	int x, y;
	int left = box->x0 > clip->x0 ? box->x0 : clip->x0;
	int top = box->y0 > clip->y0 ? box->y0 : clip->y0;
	int right = box->x1 < clip->x1 ? box->x1 : clip->x1;
	int bottom = box->y1 < clip->y1 ? box->y1 : clip->y1;
	if (left >= right || top >= bottom)
		return;
	for (y = top / 4; y < (bottom + 3) / 4; y++)
		for (x = left / 4; x < (right + 3) / 4; x++) {
			int tile = y * video.columns + x;
			if (!video.stale[tile]) {
				video.stale[tile] = 1;
				video.stale_count++;
			}
		}
}

static const struct nsfb_wii_acceleration toolkit_acceleration;

bool wii_gx_enable(nsfb_t *surface)
{
	if (surface != video.surface || video.width > video.mode->fbWidth ||
	    video.height > video.mode->efbHeight)
		return false;
	video.stale = calloc(video.columns * video.rows, 1);
	if (!video.stale)
		return false;
	video.accelerated = true;
	nsfb_wii_set_sync(surface, cpu_access);
	nsfb_wii_set_acceleration(surface, &toolkit_acceleration);
	return true;
}

int wii_gx_claim(nsfb_t *surface, nsfb_bbox_t *box)
{
	if (video.accelerated && surface == video.surface)
		return nsfb_wii_claim_native(surface, box);
	return nsfb_claim(surface, box);
}

static bool begin_drawing(nsfb_t *surface, const nsfb_bbox_t *clip)
{
	if (!video.accelerated || surface != video.surface)
		return false;
	if (!video.pending) {
		upload_page();
		GX_SetViewport(0, 0, video.width, video.height, 0, 1);
		GX_SetScissor(0, 0, video.width, video.height);
		GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
		GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
		GX_LoadTexObj(&video.page, GX_TEXMAP0);
		quad(0, 0, video.width, video.height, 1, 1);
		video.pending = true;
	}
	GX_SetScissor(
		clip->x0, clip->y0, clip->x1 - clip->x0, clip->y1 - clip->y0);
	video.changed = true;
	return true;
}

bool wii_gx_fill(nsfb_t *surface,
		 const nsfb_bbox_t *clip,
		 const nsfb_bbox_t *box,
		 uint32_t color)
{
	nsfb_bbox_t visible = {box->x0 > clip->x0 ? box->x0 : clip->x0,
			       box->y0 > clip->y0 ? box->y0 : clip->y0,
			       box->x1 < clip->x1 ? box->x1 : clip->x1,
			       box->y1 < clip->y1 ? box->y1 : clip->y1};
	if (!video.accelerated || surface != video.surface)
		return false;
	if (visible.x0 >= visible.x1 || visible.y0 >= visible.y1)
		return true;
	if (!begin_drawing(surface, clip))
		return false;
	GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetTevColor(GX_TEVREG0,
		       (GXColor){color, color >> 8, color >> 16, 255});
	GX_SetTevColorIn(
		GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
	GX_SetTevAlphaIn(
		GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
	quad(visible.x0,
	     visible.y0,
	     visible.x1 - visible.x0,
	     visible.y1 - visible.y0,
	     1,
	     1);
	gpu_damage(clip, box);
	video.gpu_rectangles++;
	return true;
}

/* Bounded content-addressed cache: decoded images can change in place and
 * FreeType can recycle glyph addresses. Hash bytes, not object pointers. */
#define GX_CACHE_LIMIT (4u * 1024u * 1024u)
#define GX_CACHE_SLOTS 96
static struct gx_texture {
	uint8_t *pixels;
	GXTexObj object;
	uint64_t hash;
	size_t bytes;
	unsigned used;
	int width, height;
} texture_cache[GX_CACHE_SLOTS];
static size_t texture_bytes;
static unsigned texture_clock;

static void release_texture(struct gx_texture *entry)
{
	if (!entry->pixels)
		return;
	/* Commands may still reference an evicted texture. */
	GX_DrawDone();
	texture_bytes -= entry->bytes;
	free(entry->pixels);
	memset(entry, 0, sizeof(*entry));
}

static uint64_t image_hash(const uint8_t *source,
			   int width,
			   int height,
			   int pitch,
			   bool alpha,
			   uint32_t color,
			   bool glyph,
			   bool canonical)
{
	uint64_t hash = UINT64_C(14695981039346656037);
	for (int y = 0; y < height; y++)
		for (int x = 0; x < width * (glyph ? 1 : 4); x++)
			hash = (hash ^ source[y * pitch + x]) *
			       UINT64_C(1099511628211);
	video.hash_bytes += (uint64_t)width * height * (glyph ? 1 : 4);
	hash = (hash ^ color) * UINT64_C(1099511628211);
	return (hash ^ (unsigned)(alpha | (glyph << 1) | (canonical << 2))) *
	       UINT64_C(1099511628211);
}

/* Valid raw RGBA sources only; reuse solely within one immutable plot call. */
uint64_t
wii_gx_image_hash(const uint8_t *source, int w, int h, int pitch, bool alpha)
{
	return image_hash(source, w, h, pitch, alpha, 0, false, false);
}

static bool draw_image(nsfb_t *surface,
		       const nsfb_bbox_t *clip,
		       const nsfb_bbox_t *box,
		       const uint8_t *source,
		       int width,
		       int height,
		       int pitch,
		       bool alpha,
		       uint32_t color,
		       bool glyph,
		       bool canonical,
		       const uint64_t *prepared)
{
	uint64_t started = gettime();
	nsfb_bbox_t visible = {box->x0 > clip->x0 ? box->x0 : clip->x0,
			       box->y0 > clip->y0 ? box->y0 : clip->y0,
			       box->x1 < clip->x1 ? box->x1 : clip->x1,
			       box->y1 < clip->y1 ? box->y1 : clip->y1};
	int x, y, slot, tw = (width + 3) & ~3, th = (height + 3) & ~3;
	size_t bytes = (size_t)tw * th * 4;
	uint64_t hash;
	struct gx_texture *entry = NULL;
	if (!video.accelerated || surface != video.surface || !source ||
	    width <= 0 || height <= 0 || tw > 1024 || th > 1024 ||
	    pitch < width * (glyph ? 1 : 4) || bytes > GX_CACHE_LIMIT)
		return false;
	if (visible.x0 >= visible.x1 || visible.y0 >= visible.y1)
		return true;
	hash = prepared ? *prepared
			: image_hash(source,
				     width,
				     height,
				     pitch,
				     alpha,
				     color,
				     glyph,
				     canonical);
	for (slot = 0; slot < GX_CACHE_SLOTS; slot++) {
		struct gx_texture *candidate = &texture_cache[slot];
		if (candidate->pixels && candidate->hash == hash &&
		    candidate->width == width && candidate->height == height) {
			entry = candidate;
			video.cache_hits++;
			break;
		}
	}
	if (!entry) {
		video.cache_misses++;
		for (;;) {
			struct gx_texture *oldest = &texture_cache[0];
			entry = NULL;
			for (slot = 0; slot < GX_CACHE_SLOTS; slot++) {
				struct gx_texture *candidate =
					&texture_cache[slot];
				if (!candidate->pixels)
					entry = candidate;
				else if (candidate->used < oldest->used ||
					 !oldest->pixels)
					oldest = candidate;
			}
			if (entry && texture_bytes + bytes <= GX_CACHE_LIMIT)
				break;
			release_texture(oldest);
		}
		entry->pixels = memalign(32, bytes);
		if (!entry->pixels)
			return false;
		memset(entry->pixels, 0, bytes);
		for (y = 0; y < height; y++)
			for (x = 0; x < width; x++) {
				size_t offset = ((y / 4) * (tw / 4) + x / 4) *
							64 +
						((y % 4) * 4 + x % 4) * 2;
				const uint8_t *pixel = source + y * pitch +
						       x * (glyph ? 1 : 4);
				entry->pixels[offset] =
					glyph ? *pixel
					      : (alpha ? pixel[canonical ? 0
									 : 3]
						       : 255);
				entry->pixels[offset + 1] =
					glyph ? color
					      : pixel[canonical ? 3 : 0];
				entry->pixels[offset + 32] =
					glyph ? color >> 8
					      : pixel[canonical ? 2 : 1];
				entry->pixels[offset + 33] =
					glyph ? color >> 16
					      : pixel[canonical ? 1 : 2];
			}
		DCStoreRange(entry->pixels, bytes);
		GX_InvalidateTexAll();
		GX_InitTexObj(&entry->object,
			      entry->pixels,
			      tw,
			      th,
			      GX_TF_RGBA8,
			      GX_CLAMP,
			      GX_CLAMP,
			      GX_FALSE);
		GX_InitTexObjLOD(&entry->object,
				 GX_NEAR,
				 GX_NEAR,
				 0,
				 0,
				 0,
				 GX_FALSE,
				 GX_FALSE,
				 GX_ANISO_1);
		entry->hash = hash;
		entry->width = width;
		entry->height = height;
		entry->bytes = bytes;
		texture_bytes += bytes;
	}
	video.texture_ticks += gettime() - started;
	entry->used = ++texture_clock;
	if (!begin_drawing(surface, clip))
		return false;
	GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
	GX_SetBlendMode(alpha || glyph ? GX_BM_BLEND : GX_BM_NONE,
			GX_BL_SRCALPHA,
			GX_BL_INVSRCALPHA,
			GX_LO_CLEAR);
	GX_LoadTexObj(&entry->object, GX_TEXMAP0);
	float u = (float)width / tw, v = (float)height / th;
	float dx = (float)box->x1 - box->x0, dy = (float)box->y1 - box->y0;
	textured_quad(visible.x0,
		      visible.y0,
		      visible.x1 - visible.x0,
		      visible.y1 - visible.y0,
		      ((float)visible.x0 - box->x0) / dx * u,
		      ((float)visible.y0 - box->y0) / dy * v,
		      ((float)visible.x1 - box->x0) / dx * u,
		      ((float)visible.y1 - box->y0) / dy * v);
	gpu_damage(clip, box);
	if (glyph)
		video.gpu_glyphs++;
	else
		video.gpu_images++;
	return true;
}

bool wii_gx_image(nsfb_t *surface,
		  const nsfb_bbox_t *clip,
		  const nsfb_bbox_t *box,
		  const uint8_t *pixels,
		  int width,
		  int height,
		  int pitch,
		  bool alpha,
		  uint32_t color,
		  bool glyph)
{
	return draw_image(surface,
			  clip,
			  box,
			  pixels,
			  width,
			  height,
			  pitch,
			  alpha,
			  color,
			  glyph,
			  false,
			  NULL);
}

bool wii_gx_image_prehashed(nsfb_t *surface,
			    const nsfb_bbox_t *clip,
			    const nsfb_bbox_t *box,
			    const uint8_t *pixels,
			    int w,
			    int h,
			    int pitch,
			    bool alpha,
			    uint64_t hash)
{
	return draw_image(surface,
			  clip,
			  box,
			  pixels,
			  w,
			  h,
			  pitch,
			  alpha,
			  0,
			  false,
			  false,
			  &hash);
}

static bool
toolkit_fill(nsfb_t *surface, const nsfb_bbox_t *box, uint32_t color)
{
	nsfb_bbox_t clip;
	nsfb_plot_get_clip(surface, &clip);
	return wii_gx_fill(surface, &clip, box, color);
}
static bool toolkit_bitmap(nsfb_t *surface,
			   const nsfb_bbox_t *box,
			   const uint32_t *pixels,
			   int width,
			   int height,
			   int stride,
			   bool alpha)
{
	nsfb_bbox_t clip;
	nsfb_plot_get_clip(surface, &clip);
	return draw_image(surface,
			  &clip,
			  box,
			  (const uint8_t *)pixels,
			  width,
			  height,
			  stride * 4,
			  alpha,
			  0,
			  false,
			  true,
			  NULL);
}
static bool toolkit_rectangle(nsfb_t *surface,
			      const nsfb_bbox_t *box,
			      int width,
			      uint32_t color,
			      bool dotted,
			      bool dashed)
{
	nsfb_bbox_t clip, edge;
	if (width != 1 || dotted || dashed || box->x0 > box->x1 ||
	    box->y0 > box->y1 || box->x1 == INT32_MAX || box->y1 == INT32_MAX)
		return false;
	nsfb_plot_get_clip(surface, &clip);
	/* Match libnsfb's four one-pixel lines, including its exclusive ends.
	 */
	edge = (nsfb_bbox_t){box->x0, box->y0, box->x1, box->y0 + 1};
	if (!wii_gx_fill(surface, &clip, &edge, color))
		return false;
	edge = (nsfb_bbox_t){box->x0, box->y1, box->x1, box->y1 + 1};
	wii_gx_fill(surface, &clip, &edge, color);
	edge = (nsfb_bbox_t){box->x0, box->y0, box->x0 + 1, box->y1};
	wii_gx_fill(surface, &clip, &edge, color);
	edge = (nsfb_bbox_t){box->x1, box->y0, box->x1 + 1, box->y1};
	wii_gx_fill(surface, &clip, &edge, color);
	return true;
}
static bool toolkit_line(nsfb_t *surface,
			 const nsfb_bbox_t *line,
			 const nsfb_plot_pen_t *pen)
{
	nsfb_bbox_t clip, box = *line;
	if (pen->stroke_type != NFSB_PLOT_OPTYPE_SOLID ||
	    pen->stroke_width > 1 || (box.x0 != box.x1 && box.y0 != box.y1))
		return false;
	nsfb_plot_get_clip(surface, &clip);
	if (box.y0 == box.y1) {
		if (!nsfb_plot_clip(&clip, &box))
			return true;
		box.y1 = box.y0 + 1;
	} else {
		if (!nsfb_plot_clip_line(&clip, &box))
			return true;
		box.x1 = box.x0 + 1;
		if (box.y0 > box.y1) {
			int top = box.y1 + 1;
			box.y1 = box.y0 + 1;
			box.y0 = top;
		}
	}
	return wii_gx_fill(surface, &clip, &box, pen->stroke_colour);
}
static const struct nsfb_wii_acceleration toolkit_acceleration =
	{toolkit_line, toolkit_fill, toolkit_bitmap, toolkit_rectangle};

void wii_present_frame(void)
{
	uint64_t started;
	if (video.surface == NULL || !video.changed)
		return;
	started = gettime();
	save_page();
	upload_page();
	GX_SetViewport(0, 0, video.mode->fbWidth, video.mode->efbHeight, 0, 1);
	GX_SetScissor(0, 0, video.mode->fbWidth, video.mode->efbHeight);
	GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
	GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
	GX_LoadTexObj(&video.page, GX_TEXMAP0);
	quad(0, 0, video.width, video.height, 1, 1);
	if (video.cursor != NULL) {
		GX_SetBlendMode(GX_BM_BLEND,
				GX_BL_SRCALPHA,
				GX_BL_INVSRCALPHA,
				GX_LO_CLEAR);
		GX_LoadTexObj(&video.pointer, GX_TEXMAP0);
		quad(video.cursor_x,
		     video.cursor_y,
		     video.cursor_w,
		     video.cursor_h,
		     video.cursor_w / 64.0f,
		     video.cursor_h / 64.0f);
	}
	GX_DrawDone();
	VIDEO_WaitVSync();
	GX_CopyDisp(MEM_K0_TO_K1(video.xfb), GX_FALSE);
	GX_DrawDone();
	video.changed = video.cursor_uploaded = false;
	video.frames++;
	video.ticks += gettime() - started;
}

void wii_present_redraw(bool start)
{
	if (start)
		video.redraw_started = gettime();
	else {
		video.redraw_ticks += gettime() - video.redraw_started;
		video.redraws++;
	}
}

/* Freeze page-load metrics before correctness probes and artifact export. */
void wii_present_profile_checkpoint(void)
{
	scene.redraw = video.redraw_ticks;
	scene.present = video.ticks;
	scene.upload = video.upload_ticks;
	scene.sync = video.sync_ticks;
	scene.texture = video.texture_ticks;
	scene.frames = video.frames;
	scene.redraws = video.redraws;
	scene.tiles = video.tiles;
	scene.flushed = video.flushed;
	scene.readbacks = video.readbacks;
}

void wii_present_stats(FILE *report)
{
	fprintf(report,
		"gx_rectangles=%u\ngx_images=%u\ngx_glyphs=%u\ngx_readbacks=%u\n",
		video.gpu_rectangles,
		video.gpu_images,
		video.gpu_glyphs,
		video.readbacks);
	fprintf(report,
		"redraws=%u\nredraw_us=%llu\nupload_us=%llu\nsync_us=%llu\ntexture_us=%llu\ntexture_cache_hits=%u\ntexture_cache_misses=%u\ntexture_cache_bytes=%u\n",
		video.redraws,
		(unsigned long long)ticks_to_microsecs(video.redraw_ticks),
		(unsigned long long)ticks_to_microsecs(video.upload_ticks),
		(unsigned long long)ticks_to_microsecs(video.sync_ticks),
		(unsigned long long)ticks_to_microsecs(video.texture_ticks),
		video.cache_hits,
		video.cache_misses,
		(unsigned)texture_bytes);
	fprintf(report,
		"scene_redraw_us=%llu\nscene_present_us=%llu\nscene_upload_us=%llu\nscene_sync_us=%llu\nscene_texture_us=%llu\nscene_frames=%u\nscene_redraws=%u\nscene_tiles=%u\nscene_flushed_bytes=%u\nscene_readbacks=%u\n",
		(unsigned long long)ticks_to_microsecs(scene.redraw),
		(unsigned long long)ticks_to_microsecs(scene.present),
		(unsigned long long)ticks_to_microsecs(scene.upload),
		(unsigned long long)ticks_to_microsecs(scene.sync),
		(unsigned long long)ticks_to_microsecs(scene.texture),
		scene.frames,
		scene.redraws,
		scene.tiles,
		scene.flushed,
		scene.readbacks);
	fprintf(report,
		"texture_hash_bytes=%llu\n",
		(unsigned long long)video.hash_bytes);
	for (unsigned i = 0; i < 16 && video.sync_sources[i].pc; i++)
		fprintf(report,
			"sync_source_%u=0x%lx,%u,%llu\n",
			i,
			(unsigned long)video.sync_sources[i].pc,
			video.sync_sources[i].count,
			(unsigned long long)ticks_to_microsecs(
				video.sync_sources[i].ticks));
	GXColor sample;
	GX_PeekARGB(80, 200, &sample);
	fprintf(report, "gpu_sample=%u,%u,%u\n", sample.r, sample.g, sample.b);
	fprintf(report,
		"present_frames=%u\nconverted_tiles=%u\nflushed_bytes=%u\npresent_us=%llu\n",
		video.frames,
		video.tiles,
		video.flushed,
		(unsigned long long)ticks_to_microsecs(video.ticks));
}

void wii_present_stop(void)
{
	if (video.surface == NULL)
		return;
	for (int slot = 0; slot < GX_CACHE_SLOTS; slot++)
		release_texture(&texture_cache[slot]);
	texture_clock = 0;
	nsfb_wii_set_acceleration(video.surface, NULL);
	nsfb_wii_set_sync(video.surface, NULL);
	nsfb_wii_set_presenter(video.surface, NULL, NULL, NULL);
	GX_DrawDone();
	/* SDL's shutdown frees its own display buffer. Stop scanning ours
	 * first. */
	VIDEO_SetBlack(TRUE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	free(video.texture);
	free(video.dirty);
	free(video.stale);
	free(video.xfb);
	memset(&video, 0, sizeof(video));
}

/* Developer smoke captures the GPU EFB, including the separate cursor. */
bool wii_present_capture(const char *path)
{
	uint8_t *texture;
	size_t size;
	int x, y, width, height, columns;
	FILE *file;
	if (video.surface == NULL)
		return false;
	width = video.mode->fbWidth;
	height = video.mode->efbHeight;
	columns = (width + 3) / 4;
	size = columns * ((height + 3) / 4) * 64;
	texture = memalign(32, size);
	if (texture == NULL)
		return false;
	GX_SetTexCopySrc(0, 0, width, height);
	GX_SetTexCopyDst(width, height, GX_TF_RGBA8, GX_FALSE);
	GX_CopyTex(texture, GX_FALSE);
	GX_PixModeSync();
	GX_DrawDone();
	DCInvalidateRange(texture, size);
	file = fopen(path, "wb");
	if (file != NULL) {
		fprintf(file, "P6\n%d %d\n255\n", width, height);
		for (y = 0; y < height; y++)
			for (x = 0; x < width; x++) {
				unsigned char rgb[3];
				size_t offset = ((y / 4) * columns + x / 4) *
							64 +
						((y % 4) * 4 + x % 4) * 2;
				rgb[0] = texture[offset + 1];
				rgb[1] = texture[offset + 32];
				rgb[2] = texture[offset + 33];
				fwrite(rgb, 1, 3, file);
			}
		if (fclose(file) != 0) {
			free(texture);
			return false;
		}
	}
	free(texture);
	return file != NULL;
}


bool wii_present_test_cursor(void)
{
	nsfb_bbox_t original, moved;
	unsigned before;
	if (video.surface == NULL)
		return true; /* legacy mode uses software cursor */
	wii_present_frame();
	nsfb_cursor_loc_get(video.surface, &original);
	moved = original;
	moved.x0 += 4;
	moved.y0 += 4;
	before = video.tiles;
	nsfb_cursor_loc_set(video.surface, &moved);
	wii_present_frame();
	nsfb_cursor_loc_set(video.surface, &original);
	wii_present_frame();
	return video.tiles == before;
}

/* Deterministic target checks exercise ownership transitions and framebuffer
 * copy, rather than merely counting calls made to the GPU. Restore the UI. */
bool wii_present_test_gx(void)
{
	nsfb_bbox_t area = {0, 0, 96, 48}, clip, red = {8, 8, 32, 32};
	nsfb_bbox_t sample = {16, 16, 17, 17}, moved = {48, 8, 72, 32};
	nsfb_colour_t *saved, result;
	bool ok = true;
	uint8_t rgba[16] = {
		0, 0, 255, 128, 0, 0, 255, 128, 0, 0, 255, 128, 0, 0, 255, 128};
	uint8_t coverage = 255;
	nsfb_plot_pen_t pen = {0};
	if (!video.accelerated)
		return true;
	saved = malloc(96 * 48 * sizeof(*saved));
	if (!saved)
		return false;
	nsfb_plot_get_clip(video.surface, &clip);
	nsfb_plot_set_clip(video.surface, &area);
	nsfb_plot_readrect(video.surface, &area, saved);
	ok &= wii_gx_fill(video.surface, &area, &area, 0xffffff);
	nsfb_bbox_t limited = {12, 12, 28, 28};
	ok &= wii_gx_fill(video.surface, &limited, &red, 0xff);
	nsfb_bbox_t outside = {10, 10, 11, 11};
	nsfb_plot_readrect(video.surface, &outside, &result);
	ok &= (result & 0xf8fcf8) == 0xf8fcf8;
	ok &= wii_gx_image(
		video.surface, &area, &red, rgba, 2, 2, 8, true, 0, false);
	/* A CPU read must see the alpha-composited GPU result. */
	nsfb_plot_readrect(video.surface, &sample, &result);
	ok &= (result & 255) >= 120 && (result & 255) <= 136 &&
	      ((result >> 16) & 255) >= 120 && ((result >> 16) & 255) <= 136 &&
	      ((result >> 8) & 255) == 0;
	/* CPU fallback between GPU operations, then glyph and scrolling. */
	pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
	pen.stroke_width = 1;
	pen.stroke_colour = 0x00ff00;
	nsfb_plot_line(video.surface, &red, &pen);
	ok &= wii_gx_image(video.surface,
			   &area,
			   &red,
			   &coverage,
			   1,
			   1,
			   1,
			   true,
			   0xff,
			   true);
	nsfb_plot_copy(video.surface, &red, video.surface, &moved);
	sample = (nsfb_bbox_t){56, 16, 57, 17};
	nsfb_plot_readrect(video.surface, &sample, &result);
	ok &= (result & 0xf8fcf8) == 0xf8;
	/* Same image pointer, different bytes: stale texture reuse is
	 * forbidden. */
	memset(rgba, 255, sizeof(rgba));
	ok &= wii_gx_image(
		video.surface, &area, &moved, rgba, 2, 2, 8, true, 0, false);
	nsfb_plot_readrect(video.surface, &sample, &result);
	ok &= (result & 0xf8fcf8) == 0xf8fcf8;
	nsfb_plot_bitmap(video.surface, &area, saved, 96, 48, 96, false);
	nsfb_update(video.surface, &area);
	nsfb_plot_set_clip(video.surface, &clip);
	free(saved);
	wii_present_frame();
	return ok;
}
