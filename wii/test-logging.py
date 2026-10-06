#!/usr/bin/env python3
"""Check the real log renderer preserves formatting in SD and agent streams."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "utils/log.c").read_text()
a = source.index("static void\nnetsurf_render_log")
b = source.index("static void\nnetsurf_render_log(", a)
b = source.index("\n}", b) + 2
renderer = source[a:b]
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / "test.c").write_text("""#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef struct { int namelen; const char *name; } category;
typedef struct { int level, filenamelen, lineno, funcnamelen;
 const char *filename, *funcname; category *category; } nslog_entry_context_t;
static FILE *logfile;
static const char *nslog_gettime(void) { return "time"; }
static const char *nslog_short_level_name(int level) { (void)level; return "WARN"; }
""" + renderer + """
static void emit(const char *format, ...) {
 category cat={4,"test"}; nslog_entry_context_t ctx={4,6,12,4,"test.c","emit",&cat};
 va_list args;va_start(args,format);netsurf_render_log(NULL,&ctx,format,args);va_end(args);
}
int main(int argc, char **argv) {
 assert(argc==2 && freopen(argv[1],"w+",stderr));
 logfile=tmpfile();assert(logfile);
 emit("value=%s %d %zu","hello",42,(size_t)123);
 fflush(logfile);rewind(logfile);
 char sd[256]={0},network[256]={0};
 assert(fread(sd,1,sizeof(sd)-1,logfile)>0);
 fflush(stderr);rewind(stderr);
 assert(fread(network,1,sizeof(network)-1,stderr)>0);
 assert(strcmp(sd,network)==0 && strstr(sd,"value=hello 42 123\\n"));
 fclose(logfile);logfile=stderr;
 assert(freopen(argv[1],"w+",stderr));
 emit("single %d",7);fflush(stderr);rewind(stderr);
 memset(network,0,sizeof(network));
 assert(fread(network,1,sizeof(network)-1,stderr)>0);
 const char *line=strchr(network,'\\n');assert(line && line[1]=='\\0');
 return 0;
}
""")
    subprocess.run(["cc", "-DGEKKO", "-DNETSURF_HBC_AGENT", str(p / "test.c"),
                    "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test"), str(p / "network.log")], check=True)
print("PASS: SD/agent log formatting and no duplicate stderr records")
