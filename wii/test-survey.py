#!/usr/bin/env python3
"""Check the actual DOM-text capture's byte/depth limits and borrowed references."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "frontends/framebuffer/gui.c").read_text()
start = source.index("static void wii_site_dom_text(")
helper = source[start:source.index("static void wii_sites_poll(", start)]
harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef enum {DOM_ELEMENT_NODE, DOM_TEXT_NODE, DOM_CDATA_SECTION_NODE} dom_node_type;
typedef struct {char *data;size_t length;} dom_string;
typedef struct node {dom_node_type type;dom_string *text;struct node *child,*next;int refs;} dom_node;
struct box {struct box *children,*next,*parent;char *text;size_t length;int visible;};
static int box_visible(struct box *box) {return box->visible;}
#define DOM_NO_ERR 0
static dom_node *ref(dom_node *n) {if(n)n->refs++;return n;}
static void dom_node_unref(dom_node *n) {if(n){assert(n->refs>0);n->refs--;}}
static int dom_node_get_node_type(dom_node *n,dom_node_type *type) {*type=n->type;return 0;}
static int dom_node_get_node_value(dom_node *n,dom_string **text) {*text=n->text;return 0;}
static size_t dom_string_byte_length(dom_string *s) {return s->length;}
static char *dom_string_data(dom_string *s) {return s->data;}
static void dom_string_unref(dom_string *s) {(void)s;}
static int dom_node_get_first_child(dom_node *n,dom_node **child) {*child=ref(n->child);return 0;}
static int dom_node_get_next_sibling(dom_node *n,dom_node **next) {*next=ref(n->next);return 0;}
'''+helper+r'''
int main(void) {
 dom_string first={"ABC",3},second={"DEF",3};
 dom_node b={DOM_TEXT_NODE,&second,NULL,NULL,0};
 dom_node a={DOM_TEXT_NODE,&first,NULL,&b,0};
 dom_node root={DOM_ELEMENT_NODE,NULL,&a,NULL,1};
 FILE *out=tmpfile();assert(out);size_t remaining=4;
 wii_site_dom_text(out,&root,&remaining,0);assert(remaining==0 && ftell(out)==4);
 rewind(out);char text[5]={0};assert(fread(text,1,4,out)==4 && !strcmp(text,"ABCD"));
 assert(root.refs==1 && a.refs==0 && b.refs==0);fclose(out);
 dom_node deep[130]={0};for(int i=0;i<129;i++)deep[i].child=&deep[i+1];
 deep[129].type=DOM_TEXT_NODE;deep[129].text=&first;deep[0].refs=1;
 out=tmpfile();remaining=100;wii_site_dom_text(out,deep,&remaining,0);
 assert(ftell(out)==0 && remaining==100);for(int i=1;i<130;i++)assert(deep[i].refs==0);fclose(out);
 dom_string large={malloc(3*1024*1024),3*1024*1024};assert(large.data);memset(large.data,'x',large.length);
 a.text=&large;a.next=NULL;out=tmpfile();remaining=2*1024*1024;
 wii_site_dom_text(out,&root,&remaining,0);assert(remaining==0 && ftell(out)==2*1024*1024 && a.refs==0);
 fclose(out);
 struct box visible={NULL,NULL,NULL,"score",5,1}, hidden={NULL,NULL,NULL,"hidden",6,0};
 struct box boxes={&visible,NULL,NULL,NULL,0,1};
 visible.next=&hidden;visible.parent=hidden.parent=&boxes;
 out=tmpfile();wii_site_layout_text(out,&boxes);assert(ftell(out)==6);fclose(out);
 visible.text=large.data;visible.length=large.length;
 out=tmpfile();wii_site_layout_text(out,&boxes);assert(ftell(out)==2*1024*1024);fclose(out);
 free(large.data);puts("PASS: actual DOM text capture byte/depth limits and node references");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "survey.c").write_text(harness)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(path / "survey.c"), "-o", str(path / "survey")], check=True)
    subprocess.run([str(path / "survey")], check=True)
