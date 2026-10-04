#!/usr/bin/env python3
"""Run the bundled Duktape with the real Wii allocator and timeout functions."""

from pathlib import Path
import subprocess
import tempfile
import re

root = Path(__file__).resolve().parents[1]
engine = root / "content/handlers/javascript/duktape"
source = (engine / "dukky.c").read_text()
allocation = source[
    source.index("#ifdef GEKKO\n/* Bound") : source.index(
        "static duk_ret_t dukky_setup_heap"
    )
]
start = source.index("duk_bool_t dukky_check_timeout(")
end = source.index("static void dukky_dump_error", start)
page_flags = re.search(
    r"duk_pcompile_lstring_filename\(\s*CTX,\s*([^,]+),",
    source[source.index("bool\njs_exec(") :],
).group(1)
events = (engine / "EventTarget.bnd").read_text()
event_helpers = events[
    events.index("static void event_target_register_listener") : events.index(
        "\n%}", events.index("static void event_target_register_listener")
    )
]
shuffle = source[
    source.index("void dukky_shuffle_array(") : source.index(
        "\n}", source.index("void dukky_shuffle_array(")
    )
    + 2
]
document = (engine / "Document.bnd").read_text()
a = document.index("static duk_ret_t document_push_title(")
title_push = document[a : document.index("\n}", a) + 2]
a = document.index("getter Document::title()")
a = document.index("%{", a) + 2
title_getter = document[a : document.index("%}", a)]
navigator = (engine / "Navigator.bnd").read_text()
a = navigator.index("getter Navigator::cookieEnabled()")
a = navigator.index("%{", a) + 2
cookie_enabled_getter = navigator[a : navigator.index("%}", a)]
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    for name in ["duktape.c", "duktape.h", "duk_config.h"]:
        (p / name).write_bytes((engine / name).read_bytes())
    (p / "duk_custom.h").write_text("""#define DUK_USE_INTERRUPT_COUNTER
extern duk_bool_t dukky_check_timeout(void *);
#define DUK_USE_EXEC_TIMEOUT_CHECK dukky_check_timeout
""")
    harness = (
        """#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "duktape.h"
#define NSUERROR_OK 0
#define PAGE_COMPILE_FLAGS """
        + page_flags
        + """
typedef struct jsheap {size_t allocated_bytes; uint64_t exec_start_time,timeout_ms; unsigned exec_depth;} jsheap;
static uint64_t clock_ms=100;
static int nsu_getmonotonic_ms(uint64_t *out) {*out=++clock_ms;return 0;}
"""
        + """typedef struct {const char *data;size_t length;} dom_string;
static dom_string fake_title;
static int title_refs;
struct fake_document {struct {void *node;} parent;};
static struct fake_document title_owner={{(void*)1}}, *priv=&title_owner;
#define DOM_NO_ERR 0
static int dom_html_document_get_title(void *node,dom_string **out) {
 assert(node);*out=&fake_title;title_refs++;return 0;
}
static const char *dom_string_data(dom_string *s) {return s->data;}
static size_t dom_string_byte_length(dom_string *s) {return s->length;}
static void dom_string_unref(dom_string *s) {assert(s && title_refs>0);title_refs--;}
"""
        + title_push
        + """
static duk_ret_t test_title_getter(duk_context *ctx,void *unused) {
 (void)unused;
"""
        + title_getter
        + """
}
"""
        + """static duk_ret_t navigator_cookie_enabled(duk_context *ctx) {
"""
        + cookie_enabled_getter
        + """
}
"""
        + """typedef enum {ELF_NONE=0,ELF_CAPTURE=1,ELF_PASSIVE=2,ELF_ONCE=4} event_listener_flags;\n"""
        + shuffle
        + event_helpers
        + allocation
        + source[start:end]
        + """static void dukky_dump_error(duk_context *ctx) {(void)ctx;}
"""
        + source[
            source.index("static void dukky_reset_start_time") : source.index(
                "void dukky_push_generics"
            )
        ]
        + """static duk_ret_t nested(duk_context *ctx) {
 duk_get_global_string(ctx,"noop"); dukky_pcall(ctx,0,true); duk_pop(ctx); return 0;
}
int main(void) {
 jsheap heap={0}; heap.timeout_ms=5;
 assert(!dukky_check_timeout(&heap));
 heap.exec_start_time=clock_ms;
 clock_ms+=4; assert(dukky_check_timeout(&heap));
 heap.exec_start_time=0;
 duk_context *ctx=duk_create_heap(dukky_alloc_function,dukky_realloc_function,dukky_free_function,&heap,NULL);
 assert(ctx);
 duk_push_object(ctx);
 duk_push_string(ctx,"cookieEnabled");
 duk_push_c_function(ctx,navigator_cookie_enabled,0);
 duk_def_prop(ctx,-3,DUK_DEFPROP_HAVE_GETTER);
 duk_put_global_string(ctx,"navigator");
 assert(duk_peval_string(ctx,"navigator.cookieEnabled === true")==0);
 assert(duk_get_boolean(ctx,-1));duk_pop(ctx);
 fake_title.data="title";fake_title.length=5;
 assert(duk_safe_call(ctx,test_title_getter,NULL,0,1)==0);
 assert(title_refs==0 && strcmp(duk_get_string(ctx,-1),"title")==0);duk_pop(ctx);
 char *oversized_title=calloc(1,WII_JS_HEAP_LIMIT+1);assert(oversized_title);
 fake_title.data=oversized_title;fake_title.length=WII_JS_HEAP_LIMIT+1;
 assert(duk_safe_call(ctx,test_title_getter,NULL,0,1)!=0);
 assert(title_refs==0);duk_pop(ctx);free(oversized_title);
 duk_push_string(ctx,"first page script");
 assert(duk_pcompile_lstring_filename(ctx,PAGE_COMPILE_FLAGS,
  "\\"use strict\\"; var pageShared=42; function pageFunction(){return pageShared;}",
  strlen("\\"use strict\\"; var pageShared=42; function pageFunction(){return pageShared;}"))==0);
 assert(duk_pcall(ctx,0)==0); duk_pop(ctx);
 assert(duk_get_global_string(ctx,"pageShared") && duk_get_int(ctx,-1)==42); duk_pop(ctx);
 assert(duk_peval_string(ctx,"pageFunction()") == 0 && duk_get_int(ctx,-1)==42); duk_pop(ctx);
 duk_push_array(ctx); duk_put_global_string(ctx,"listeners");
 duk_push_c_function(ctx,nested,0); duk_put_global_string(ctx,"listener");
 duk_get_global_string(ctx,"listeners"); duk_get_global_string(ctx,"listener");
 event_target_register_listener(ctx,ELF_PASSIVE);
 assert(duk_get_top(ctx)==0);
 duk_get_global_string(ctx,"listeners"); duk_get_global_string(ctx,"listener");
 event_target_register_listener(ctx,ELF_NONE);
 duk_get_global_string(ctx,"listeners"); assert(duk_get_length(ctx,-1)==1); duk_pop(ctx);
 duk_get_global_string(ctx,"listeners"); duk_get_global_string(ctx,"listener");
 event_target_unregister_listener(ctx,ELF_NONE);
 assert(duk_get_top(ctx)==0);
 duk_get_global_string(ctx,"listeners"); assert(!duk_get_prop_index(ctx,-1,0)); duk_pop_2(ctx);
 duk_get_global_string(ctx,"listeners"); duk_get_global_string(ctx,"listener");
 event_target_register_listener(ctx,ELF_NONE);
 duk_get_global_string(ctx,"listeners"); assert(duk_get_prop_index(ctx,-1,0));
 assert(!duk_get_prop_index(ctx,-1,2)); duk_pop_n(ctx,3);
 size_t baseline=heap.allocated_bytes;
 unsigned char *block=dukky_alloc_function(&heap,32); assert(block); block[0]=42;
 assert(!dukky_realloc_function(&heap,block,WII_JS_HEAP_LIMIT));
 assert(block[0]==42 && heap.allocated_bytes==baseline+32+sizeof(union dukky_allocation));
 block=dukky_realloc_function(&heap,block,8); assert(block && block[0]==42);
 dukky_free_function(&heap,block); assert(heap.allocated_bytes==baseline);
 heap.exec_start_time=clock_ms; heap.timeout_ms=100;
 assert(duk_peval_string(ctx,"var sum=0;for(var n=0;n<100;n++)sum+=n;sum;")==0);
 assert(duk_get_int(ctx,-1)==4950); duk_pop(ctx);
 heap.exec_start_time=clock_ms; heap.timeout_ms=5;
 assert(duk_peval_string(ctx,"while(true){}")!=0); duk_pop(ctx);
 heap.exec_start_time=0;
 duk_push_c_function(ctx,nested,0); duk_put_global_string(ctx,"nested");
 assert(duk_peval_string(ctx,"function noop(){}") == 0); duk_pop(ctx);
 dukky_reset_start_time(ctx);
 assert(duk_peval_string(ctx,"while(true){nested();}")!=0); duk_pop(ctx);
 dukky_end_execution(ctx); assert(heap.exec_depth==0);
 heap.exec_start_time=0;
 assert(duk_peval_string(ctx,"new Array(2000000).join('xxxxxx')")!=0); duk_pop(ctx);
 assert(heap.allocated_bytes<=WII_JS_HEAP_LIMIT);
 duk_gc(ctx,0);
 assert(duk_peval_string(ctx,"6*7")==0 && duk_get_int(ctx,-1)==42); duk_pop(ctx);
 duk_destroy_heap(ctx); assert(heap.allocated_bytes==0);
 return 0;
}
"""
    )
    (p / "test.c").write_text(harness)
    subprocess.run(
        [
            "cc",
            "-O1",
            "-DGEKKO",
            "-DDUK_OPT_HAVE_CUSTOM_H",
            "-I" + str(p),
            str(p / "test.c"),
            str(p / "duktape.c"),
            "-lm",
            "-o",
            str(p / "test"),
        ],
        check=True,
    )
    subprocess.run([str(p / "test")], check=True, timeout=20)
print(
    "PASS: actual Duktape execution, cookie capability, title OOM cleanup, strict page globals, listener identity/removal, timeout, bounded allocation, realloc failure, OOM recovery, teardown"
)
