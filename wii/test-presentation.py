#!/usr/bin/env python3
"""Exercise the real presenter with a fake SDK; target tests validate GX output."""

import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
source = (root / "frontends/framebuffer/wii_present.c").read_text()
with tempfile.TemporaryDirectory(prefix="wii-present-") as directory:
    tmp = pathlib.Path(directory)
    (tmp / "ogc").mkdir()
    (tmp / "malloc.h").write_text("")
    (tmp / "ogc/lwp_watchdog.h").write_text("")
    functions = set(re.findall(r"\b(GX_[A-Za-z0-9_]+)\s*\(", source)) - {
        "GX_CACHE_LIMIT",
        "GX_CACHE_SLOTS",
    }
    constants = (
        set(re.findall(r"\bGX_[A-Z][A-Z0-9_]+\b", source))
        - functions
        - {"GX_CACHE_LIMIT", "GX_CACHE_SLOTS"}
    )
    header = """#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int GXTexObj;
typedef struct {unsigned char r,g,b,a;} GXColor;
typedef float Mtx[3][4], Mtx44[4][4];
typedef struct {int fbWidth, efbHeight, xfbHeight, aa, field_rendering, viHeight; int sample_pattern[12][2], vfilter[7];} GXRModeObj;
static GXRModeObj test_mode={16,8,8};
static GXRModeObj TVPal528IntDf={16,8,8}, TVPal576IntDfScale={16,8,8};
#define ATTRIBUTE_ALIGN(n)
#define MEM_K0_TO_K1(x) (x)
#define memalign(a,n) malloc(n)
#define guOrtho(...) ((void)0)
#define guMtxIdentity(...) ((void)0)
#define guMtxTransApply(...) ((void)0)
#define DCStoreRange(...) ((void)0)
#define DCInvalidateRange(...) ((void)0)
#define VIDEO_SetNextFramebuffer(...) ((void)0)
#define VIDEO_SetBlack(...) ((void)0)
#define VIDEO_Flush(...) ((void)0)
#define VIDEO_WaitVSync(...) ((void)0)
#define SYS_AllocateFramebuffer(...) malloc(1024)
#define VIDEO_GetPreferredMode(...) (&test_mode)
static uint64_t gettime(void) {static uint64_t t; return ++t;}
#define ticks_to_microsecs(x) (x)
"""
    header += "\n".join("#define " + x + " 0" for x in sorted(constants)) + "\n"
    header += (
        "\n".join("#define " + x + "(...) ((void)0)" for x in sorted(functions)) + "\n"
    )
    (tmp / "gccore.h").write_text(header)
    harness = """#include <stdbool.h>
#include <assert.h>
#include <libnsfb.h>
#include <libnsfb_plot.h>
struct nsfb_s {int bpp; uint8_t pixels[16*8*4];};
static int stopped, native_claims, cpu_claims;
void OGC_VideoStop(void) {stopped++;}
int nsfb_get_geometry(nsfb_t *s,int *w,int *h,enum nsfb_format_e *f) {
 if(w)*w=16; if(h)*h=8; if(f)*f=s->bpp==16?NSFB_FMT_RGB565:NSFB_FMT_XRGB8888; return 0;
}
int nsfb_get_buffer(nsfb_t *s,uint8_t **p,int *pitch) {*p=s->pixels;*pitch=16*s->bpp/8;return 0;}
bool nsfb_cursor_loc_get(nsfb_t *s, nsfb_bbox_t *b) {*b=(nsfb_bbox_t){0,0,1,1};return true;}
bool nsfb_cursor_loc_set(nsfb_t *s,const nsfb_bbox_t *b) {return true;}
bool nsfb_plot_clip(const nsfb_bbox_t *c,nsfb_bbox_t *b) {return true;}
bool nsfb_plot_clip_line(const nsfb_bbox_t *c,nsfb_bbox_t *b) {return true;}
bool nsfb_plot_get_clip(nsfb_t *s,nsfb_bbox_t *b) {*b=(nsfb_bbox_t){0,0,16,8};return true;}
bool nsfb_plot_set_clip(nsfb_t *s,nsfb_bbox_t *b) {return true;}
bool nsfb_plot_readrect(nsfb_t *s,nsfb_bbox_t *b,nsfb_colour_t *p) {return true;}
bool nsfb_plot_line(nsfb_t *s,nsfb_bbox_t *b,nsfb_plot_pen_t *p) {return true;}
bool nsfb_plot_copy(nsfb_t *s,nsfb_bbox_t *b,nsfb_t *d,nsfb_bbox_t *c) {return true;}
bool nsfb_plot_bitmap(nsfb_t *s,const nsfb_bbox_t *b,const nsfb_colour_t *p,int w,int h,int stride,bool a) {return true;}
int nsfb_wii_claim_native(nsfb_t *s,nsfb_bbox_t *b) {native_claims++;return 0;}
int nsfb_claim(nsfb_t *s,nsfb_bbox_t *b) {cpu_claims++;return 0;}
int nsfb_update(nsfb_t *s,nsfb_bbox_t *b) {return 0;}
void nsfb_wii_set_acceleration(nsfb_t *s,const struct nsfb_wii_acceleration *a) {}
void nsfb_wii_set_sync(nsfb_t *s, void (*sync)(void *,const nsfb_bbox_t *,bool)) {}
void nsfb_wii_set_presenter(nsfb_t *s,void (*d)(void *,const nsfb_bbox_t *),
 void (*c)(void *,const uint32_t *,int,int,int,int,int),void *ctx) {}
"""
    harness += '#include "' + str(root / "frontends/framebuffer/wii_present.c") + '"\n'
    harness += """int main(void) {
 for(int depth=16;depth<=32;depth+=16) {
  struct nsfb_s s={0}; s.bpp=depth;
  for(unsigned i=0;i<sizeof(s.pixels);i++)s.pixels[i]=(uint8_t)i;
  assert(wii_present_start(&s)); wii_present_frame();
  assert(video.tiles==8 && video.frames==1);
  assert(video.flushed==16*8*(unsigned)depth/8);
  nsfb_bbox_t b={1,1,2,2}; damage(NULL,&b); damage(NULL,&b); wii_present_frame();
  assert(video.tiles==9 && video.frames==2);
  unsigned flushed=video.flushed;
  uint8_t original[sizeof(s.pixels)]; memcpy(original,s.pixels,sizeof(original));
  uint32_t image[]={0xff0000ff,0xffffffff}; cursor(NULL,image,1,1,1,2,3); wii_present_frame();
  assert(video.tiles==9 && video.flushed==flushed);
  cursor(NULL,image,1,1,1,-1,7); wii_present_frame();
  assert(video.tiles==9 && video.flushed==flushed);
  assert(memcmp(original,s.pixels,sizeof(original))==0);
  assert(cursor_texture[0]==255 && cursor_texture[1]==255);
  assert(cursor_texture[32]==0 && cursor_texture[33]==0);
  wii_present_frame(); assert(video.frames==4);
  cursor(NULL,image,2,1,2,-1,7); wii_present_frame();
  assert(video.frames==5 && video.tiles==9);
  /* GPU updates must not overwrite the page with an old CPU shadow. */
  assert(wii_gx_enable(&s));
  nsfb_bbox_t all={0,0,16,8}, box={2,2,6,6};
  assert(wii_gx_fill(&s,&all,&box,0xff));
  assert(video.pending && video.gpu_rectangles==1);
  assert(wii_gx_claim(&s,&box)==0);
  assert(video.pending && native_claims==depth/16 && video.readbacks==0);
  damage(NULL,&all);
  for(int i=0;i<8;i++) assert(!video.dirty[i]);
  save_page(); assert(video.stale_count && !video.pending);
  /* Fake SDK has no rasterizer: inject deterministic GPU texels to test
   * tiled readback and CPU/GPU ownership, separately from target pixels. */
  memset(video.texture,0x57,16*8*depth/8);
  nsfb_bbox_t one={2,2,3,3};
  cpu_access(NULL,&one,false);
  assert(video.stale_count==3 && video.readbacks==1);
  cpu_access(NULL,NULL,false);
  assert(!video.stale_count && video.readbacks==2);
  assert(s.pixels[4*(2*16+2)*(depth/8)/4]==(depth==16?0x57:255));
  assert(s.pixels[(2*16+2)*(depth/8)+1]==0x57);
  cpu_access(NULL,NULL,true);
  for(int i=0;i<8;i++) assert(video.dirty[i]);
  uint8_t rgba[24]={255,0,0,128,0,255,0,255,0,0,255,255};
  assert(wii_gx_image(&s,&all,&box,rgba,3,1,24,true,0,false));
  assert(video.cache_misses==1 && video.gpu_images==1);
  assert(wii_gx_image(&s,&all,&box,rgba,3,1,24,true,0,false));
  assert(video.cache_hits==1);
  rgba[0]=123; /* mutable image at the same address invalidates cache */
  assert(wii_gx_image(&s,&all,&box,rgba,3,1,24,true,0,false));
  assert(video.cache_misses==2);
  uint64_t hashed = video.hash_bytes;
  uint64_t prepared = wii_gx_image_hash(rgba,3,1,24,true);
  for(int n=0;n<64;n++) assert(wii_gx_image_prehashed(&s,&all,&box,rgba,3,1,24,true,prepared));
  assert(video.hash_bytes == hashed + 12 && video.cache_misses==2);
  rgba[0]=42; prepared=wii_gx_image_hash(rgba,3,1,24,true);
  assert(wii_gx_image_prehashed(&s,&all,&box,rgba,3,1,24,true,prepared));
  assert(video.cache_misses==3);
  uint8_t canonical[4]={255,187,102,17}; /* big-endian 0xAABBGGRR */
  assert(toolkit_bitmap(&s,&box,(const uint32_t *)canonical,1,1,1,true));
  struct gx_texture *icon=NULL;
  for(int i=0;i<GX_CACHE_SLOTS;i++)if(texture_cache[i].pixels && texture_cache[i].width==1)icon=&texture_cache[i];
  assert(icon && icon->pixels[0]==255 && icon->pixels[1]==17);
  assert(icon->pixels[32]==102 && icon->pixels[33]==187);
  struct nsfb_s offscreen={0};
  assert(wii_gx_claim(&offscreen,&box)==0 && cpu_claims==depth/16);
  assert(!wii_gx_fill(&offscreen,&all,&box,0xff));
  wii_present_stop(); assert(texture_bytes==0);
 }
 assert(stopped==2);
 /* Direct conversion, padded rows, both texture formats. */
 uint8_t input[4*24], target[64];
 for(unsigned i=0;i<sizeof(input);i++)input[i]=(uint8_t)i;
 wii_convert_tile(target,input,24,32);
 for(int y=0;y<4;y++)for(int x=0;x<4;x++) {
  int p=2*(y*4+x); assert(target[p]==input[y*24+x*4]);
  assert(target[p+1]==input[y*24+x*4+1]);
  assert(target[p+32]==input[y*24+x*4+2]);
  assert(target[p+33]==input[y*24+x*4+3]);
 }
 wii_convert_tile(target,input,24,16);
 for(int y=0;y<4;y++)for(int x=0;x<8;x++)assert(target[y*8+x]==input[y*24+x]);
 puts("PASS: dirty tiles, cursor, regional CPU/GPU ownership, native claims, image mutation, canonical icons, offscreen rejection, RGB565/RGBA8 conversion");
}
"""
    (tmp / "test.c").write_text(harness)
    subprocess.run(
        [
            "cc",
            "-std=c99",
            "-DGEKKO",
            "-I" + str(tmp),
            "-I" + str(root / "frontends"),
            "-I" + str(root / "wii/.deps/netsurf-workspace/libnsfb/include"),
            str(tmp / "test.c"),
            "-o",
            str(tmp / "test"),
        ],
        check=True,
    )
    subprocess.run([str(tmp / "test")], check=True)
