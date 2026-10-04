#ifndef NETSURF_FRAMEBUFFER_WII_COMPAT_H
#define NETSURF_FRAMEBUFFER_WII_COMPAT_H

struct gui_file_table;
struct gui_download_table;
extern struct gui_download_table wii_download_table;

struct gui_file_table *wii_get_file_table(void);
void wii_log_memory(const char *stage);
unsigned wii_heap_used(void);

#endif
