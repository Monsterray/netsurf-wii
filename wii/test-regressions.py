#!/usr/bin/env python3
"""Fast host checks of actual C control paths, with platform services stubbed.
Use --baseline to demonstrate failures against the committed source.
"""

import pathlib
import subprocess
import tempfile
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASELINE = "--baseline" in sys.argv


def read(path):
    if BASELINE:
        return subprocess.check_output(
            ["git", "show", "HEAD:" + path], cwd=ROOT, text=True
        )
    return (ROOT / path).read_text()


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def check(name, source):
    with tempfile.TemporaryDirectory(prefix="netsurf-check-") as tmp:
        src, exe = pathlib.Path(tmp) / "check.c", pathlib.Path(tmp) / "check"
        src.write_text(source)
        subprocess.run(["cc", "-w", str(src), "-o", str(exe)], check=True)
        result = subprocess.run([str(exe)], cwd=tmp, capture_output=True, text=True)
        print(("PASS" if result.returncode == 0 else "FAIL") + ": " + name)
        return result.returncode == 0


print_source = (
    r"""
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
typedef struct hlcache_handle { int unused; } hlcache_handle;
struct printer { int unused; }; struct print_settings { const char *output; };
static float done_height;
static int draws, cleanups;
static bool setup_ok = true;
static hlcache_handle *printed_content;
bool print_set_up(hlcache_handle *c,const struct printer *p,struct print_settings *s,double *h) { printed_content=c; return setup_ok; }
int content_get_height(hlcache_handle *c) { return 1; }
bool print_draw_next_page(const struct printer *p,struct print_settings *s) { draws++; done_height++; return true; }
void print_cleanup(hlcache_handle *c,const struct printer *p,struct print_settings *s) { cleanups++; free(s); }
"""
    + function(read("desktop/print.c"), "bool print_basic_run(")
    + r"""
int main(void) {
 hlcache_handle content; struct printer printer;
 struct print_settings *settings=calloc(1,sizeof(*settings));
 assert(print_basic_run(&content,&printer,settings));
 assert(draws==1 && cleanups==1);
 setup_ok=false; settings=calloc(1,sizeof(*settings));
 assert(!print_basic_run(&content,&printer,settings));
 assert(draws==1 && cleanups==1);
}
"""
)

schedule_source = (
    r"""
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
typedef int nserror;
#define NSERROR_OK 0
#define NSERROR_NOMEM 1
#define NSLOG(...) ((void)0)
struct nscallback {struct nscallback *next; struct timeval tv; void (*callback)(void *); void *p;};
static struct nscallback *schedule_list;
nserror schedule_remove(void (*c)(void *),void *p) {return 0;}
void *failed_calloc(size_t n,size_t s) {return NULL;}
#define calloc failed_calloc
"""
    + function(
        read("frontends/framebuffer/schedule.c"), "nserror framebuffer_schedule("
    )
    + r"""
void callback(void *p) {}
int main(void) {assert(framebuffer_schedule(1,callback,NULL)==NSERROR_NOMEM); assert(schedule_list==NULL);}
"""
)

# Exercise controller Home through the toolkit, not just the mapping in isolation.
patch = read("wii/patches/libnsfb-wii-endian.patch")
added = "\n".join(
    line[1:]
    for line in patch.splitlines()
    if line.startswith("+") and not line.startswith("+++")
)
controller = function(added, "static bool wii_controller_event(")
wpads = ["A", "B", "UP", "DOWN", "LEFT", "RIGHT", "PLUS", "MINUS", "1", "2", "HOME"]
keys = [
    "MOUSE_1",
    "MOUSE_3",
    "UP",
    "DOWN",
    "LEFT",
    "RIGHT",
    "PLUS",
    "MINUS",
    "PAGEUP",
    "PAGEDOWN",
    "ESCAPE",
]
event_source = (
    r"""
#include <stdbool.h>
#include <assert.h>
#include <stddef.h>
typedef unsigned int u32;
enum { NSFB_EVENT_KEY_DOWN, NSFB_EVENT_KEY_UP, NSFB_EVENT_CONTROL, NSFB_EVENT_MOVE_RELATIVE, NSFB_EVENT_MOVE_ABSOLUTE, NSFB_EVENT_RESIZE };
enum { NSFB_CONTROL_QUIT=1 };
"""
    + "\n".join(
        "#define WPAD_BUTTON_%s (1U << %d)" % (n, i) for i, n in enumerate(wpads)
    )
    + "\n"
    + "\n".join("#define NSFB_KEY_%s %d" % (n, i + 1) for i, n in enumerate(keys))
    + r"""
#define NSFB_KEY_MOUSE_5 5
enum nsfb_key_code_e { NSFB_DUMMY };
typedef struct {int type; union {int keycode; int controlcode; struct {int x,y;} vector; struct {int w,h;} resize; } value; } nsfb_event_t;
static bool wii_polled_valid=true;
static u32 wii_polled_buttons=WPAD_BUTTON_HOME, wii_previous_buttons;
"""
    + controller
    + r"""
typedef struct { int unused; struct { struct { int fb; } root; } u; } fbtk_widget_t;
typedef struct {int x0,y0;} nsfb_bbox_t;
fbtk_widget_t *fbtk_get_root_widget(fbtk_widget_t *r) {return r;}
bool nsfb_event(int f,nsfb_event_t *e,int t) {return wii_controller_event(e);}
void fbtk_warp_pointer(fbtk_widget_t *r,int x,int y,bool b) {}
void nsfb_cursor_loc_get(int f,nsfb_bbox_t *b) {}
void fbtk_click(fbtk_widget_t *r,nsfb_event_t *e) {}
void fbtk_input(fbtk_widget_t *r,nsfb_event_t *e) {}
void gui_resize(fbtk_widget_t *r,int w,int h) {}
"""
    + function(read("frontends/framebuffer/fbtk/event.c"), "bool\nfbtk_event(")
    + r"""
int main(void) {fbtk_widget_t root={0}; nsfb_event_t event;
 assert(fbtk_event(&root,&event,0));
 assert(event.type==NSFB_EVENT_CONTROL && event.value.controlcode==NSFB_CONTROL_QUIT);
}
"""
)

pdf_save_source = (
    r"""
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef int nserror;
#define NSERROR_OK 0
#define NSERROR_NOMEM 1
#define NSERROR_SAVE_FAILED 2
#define HPDF_OK 0
#define HPDF_ENCRYPT_R3 0
#define nsoption_bool(...) false
static bool pdf_failed, save_fails=true;
static int pdf_doc=1;
static nserror pdf_save_result;
static char *owner_pass, *user_pass;
void HPDF_SetPassword(int d,char *o,char *u) {}
void HPDF_SetEncryptionMode(int d,int m,int n) {}
void HPDF_Free(int d) {}
void pdf_free_images(void) {}
int HPDF_SaveToFile(int d,const char *p) {
 FILE *f=fopen(p,"wb"); if(!f) return 1;
 fputs(save_fails ? "partial" : "new",f); fclose(f); return save_fails;
}
"""
    + function(read("desktop/save_pdf.c"), "nserror save_pdf(")
    + r"""
int main(void) {
 char data[32]={0}; FILE *f=fopen("saved.pdf","wb"); fputs("previous",f); fclose(f);
 assert(save_pdf("saved.pdf")==NSERROR_SAVE_FAILED);
 f=fopen("saved.pdf","rb"); assert(f); fread(data,1,sizeof(data),f); fclose(f);
 assert(strcmp(data,"previous")==0);
 save_fails=false;
 assert(save_pdf("saved.pdf")==NSERROR_OK);
 memset(data,0,sizeof(data)); f=fopen("saved.pdf","rb"); fread(data,1,sizeof(data),f); fclose(f);
 assert(strcmp(data,"new")==0);
}
"""
)

# Exercise libfat's no-overwrite rename semantics, including rollback.
fat_pdf_source = pdf_save_source.replace(
    "#include <stdio.h>", "#define GEKKO\n#include <errno.h>\n#include <stdio.h>"
)
fat_pdf_source = fat_pdf_source.replace(
    "nserror save_pdf(",
    r"""
static bool install_fails;
int fat_rename(const char *from, const char *to) {
 FILE *f = fopen(to, "rb");
 if (f) {fclose(f); errno=EEXIST; return -1;}
 if (install_fails && strstr(from, ".tmp")) {errno=EIO; return -1;}
 return rename(from,to);
}
#define rename fat_rename
nserror save_pdf(""",
)
fat_pdf_source = fat_pdf_source.replace(
    "save_fails=false;",
    r"""
 save_fails=false; install_fails=true;
 assert(save_pdf("saved.pdf")==NSERROR_SAVE_FAILED);
 memset(data,0,sizeof(data)); f=fopen("saved.pdf","rb"); assert(f);
 fread(data,1,sizeof(data),f); fclose(f); assert(strcmp(data,"previous")==0);
 install_fails=false;
""",
)

borrowed_page_source = (
    r"""
#define GEKKO
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
typedef struct hlcache_handle {int width;} hlcache_handle;
struct print_settings {const char *output;};
struct printer {void (*print_end)(void);};
static int original_width, html_redraw_printing;
static hlcache_handle *printed_content;
int content_get_width(hlcache_handle *c) {return c->width;}
void content_reformat(hlcache_handle *c,bool background,int width,int height) {c->width=width;}
void finish(void) {}
"""
    + function(read("desktop/print.c"), "static struct hlcache_handle *\nprint_init(")
    + "\n"
    + function(read("desktop/print.c"), "bool print_cleanup(")
    + r"""
int main(void) {
 hlcache_handle c={640}; struct printer p={finish};
 struct print_settings *s=calloc(1,sizeof(*s));
 printed_content=print_init(&c,s); assert(printed_content==&c);
 c.width=595; assert(print_cleanup(&c,&p,s));
 assert(c.width==640 && printed_content==NULL);
}
"""
)

positional_io_source = (
    ""
    if BASELINE
    else r"""
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#define pread wii_pread
#define pwrite wii_pwrite
"""
    + function(read("frontends/framebuffer/wii_compat.c"), "ssize_t pread(")
    + "\n"
    + function(read("frontends/framebuffer/wii_compat.c"), "ssize_t pwrite(")
    + r"""
int main(void) {
 char b[8]={0}; int f=open("cache",O_CREAT|O_RDWR,0600);
 assert(f>=0); assert(write(f,"abcdef",6)==6); assert(lseek(f,2,SEEK_SET)==2);
 assert(wii_pwrite(f,"XY",2,3)==2); assert(lseek(f,0,SEEK_CUR)==2);
 assert(wii_pread(f,b,6,0)==6); assert(strcmp(b,"abcXYf")==0);
 assert(lseek(f,0,SEEK_CUR)==2); close(f);
 assert(wii_pread(-1,b,1,0)==-1); assert(errno==EBADF);
 assert(wii_pwrite(-1,b,1,0)==-1); assert(errno==EBADF);
}
"""
)

results = [
    check("PDF success draws pages and cleans up", print_source),
    check("scheduler reports allocation failure", schedule_source),
    check("controller Home reaches the main loop as quit", event_source),
    check("failed PDF replacement preserves the previous file", pdf_save_source),
]
if not BASELINE:
    results += [
        check("libfat PDF replacement and rollback", fat_pdf_source),
        check(
            "Wii PDF export restores the borrowed screen layout", borrowed_page_source
        ),
        check(
            "SD cache positional I/O preserves offsets and errors", positional_io_source
        ),
    ]
sys.exit(0 if all(results) else 1)
