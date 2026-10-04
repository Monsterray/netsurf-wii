#include <gccore.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <tuxedo/ppc/intrinsics.h>

#include "framebuffer/wii_compat.h"
#include "framebuffer/wii_agent.h"
#include "utils/errors.h"
#include "utils/file.h"
#include "netsurf/download.h"
#include "desktop/download.h"

/* libogc malloc starts in MEM1 and spills into MEM2. Keep both available;
 * libogc's arena high bounds exclude IOS and SDK reservations. */
u32 MALLOC_MEM2 = 1;

/* Compatibility with the rw-r-r-0644 mbedTLS 3.6.4 package. */
u64 gettime(void);

u64 gettime(void)
{
	return PPCGetTickCount();
}

static nserror wii_nsurl_to_path(struct nsurl *url, char **path_out)
{
	nserror err = default_file_table->nsurl_to_path(url, path_out);

	if (err == NSERROR_OK && (*path_out)[0] == '/' &&
			(strncmp(*path_out + 1, "sd:/", 4) == 0 ||
			 strncmp(*path_out + 1, "usb:/", 5) == 0)) {
		memmove(*path_out, *path_out + 1, strlen(*path_out));
	}

	return err;
}

struct gui_file_table *wii_get_file_table(void)
{
	static struct gui_file_table file_table;

	file_table = *default_file_table;
	file_table.nsurl_to_path = wii_nsurl_to_path;
	return &file_table;
}

unsigned wii_heap_used(void)
{
    unsigned raw = (unsigned)mallinfo().uordblks;
    unsigned gap = wii_agent_heap_gap();
    return raw >= gap ? raw - gap : raw;
}

void wii_log_memory(const char *stage)
{
	struct mallinfo heap = mallinfo();
	SYS_Report("NetSurf Wii: memory %s MEM1=%p..%p (%u free) "
		"MEM2=%p..%p (%u free) heap_used=%u heap_free=%u\n", stage,
		SYS_GetArena1Lo(), SYS_GetArena1Hi(),
		(unsigned)((uintptr_t)SYS_GetArena1Hi() - (uintptr_t)SYS_GetArena1Lo()),
		SYS_GetArena2Lo(), SYS_GetArena2Hi(),
		(unsigned)((uintptr_t)SYS_GetArena2Hi() - (uintptr_t)SYS_GetArena2Lo()),
		wii_heap_used(), (unsigned)heap.fordblks);
}

/* Downloads stay beside the application; incomplete transfers remain private. */
struct gui_download_window {
	struct download_context *context;
	FILE *file;
	char destination[512];
	char temporary[520];
};

static void wii_download_dispose(void *context)
{
	struct gui_download_window *download = context;
	if (download->file != NULL)
		fclose(download->file);
	remove(download->temporary);
	download_context_destroy(download->context);
	free(download);
}

static struct gui_download_window *wii_download_create(
		struct download_context *context, struct gui_window *parent)
{
	struct gui_download_window *download;
	const char *source = download_context_get_filename(context);
	char filename[160];
	unsigned int index, serial;
	int fd = -1;
	struct stat info;
	(void)parent;

	if (mkdir("sd:/apps/netsurf/Downloads", 0777) != 0 && errno != EEXIST)
		return NULL;
	if (source == NULL || *source == '\0')
		source = "download";
	for (index = 0; source[index] != '\0' && index < sizeof(filename) - 1; index++) {
		unsigned char c = source[index];
		filename[index] = (c < 32 || c == '/' || c == '\\' || c == ':') ? '_' : c;
	}
	filename[index] = '\0';
	if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0)
		strcpy(filename, "download");
	download = calloc(1, sizeof(*download));
	if (download == NULL)
		return NULL;
	for (serial = 0; serial < 1000; serial++) {
		if (serial == 0)
			snprintf(download->destination, sizeof(download->destination),
					"sd:/apps/netsurf/Downloads/%s", filename);
		else
			snprintf(download->destination, sizeof(download->destination),
					"sd:/apps/netsurf/Downloads/%s.%u", filename, serial);
		if (stat(download->destination, &info) == 0)
			continue;
		snprintf(download->temporary, sizeof(download->temporary),
				"%s.part", download->destination);
		fd = open(download->temporary, O_WRONLY | O_CREAT | O_EXCL, 0666);
		if (fd >= 0 || errno != EEXIST)
			break;
	}
	if (fd < 0) {
		free(download);
		return NULL;
	}
	download->file = fdopen(fd, "wb");
	if (download->file == NULL) {
		close(fd);
		remove(download->temporary);
		free(download);
		return NULL;
	}
	download->context = context;
	SYS_Report("NetSurf Wii: downloading %s\n", download->destination);
	return download;
}

static nserror wii_download_data(struct gui_download_window *download,
		const char *data, unsigned int length)
{
	if (fwrite(data, 1, length, download->file) != length) {
		return NSERROR_SAVE_FAILED;
	}
	return NSERROR_OK;
}

static void wii_download_error(struct gui_download_window *download,
		const char *error)
{
	SYS_Report("NetSurf Wii: download failed: %s\n", error);
	wii_download_dispose(download);
}

static void wii_download_done(struct gui_download_window *download)
{
	int result = fclose(download->file);
	download->file = NULL;
	if (result == 0 && rename(download->temporary, download->destination) == 0)
		SYS_Report("NetSurf Wii: downloaded %s\n", download->destination);
	else
		SYS_Report("NetSurf Wii: unable to save download\n");
	wii_download_dispose(download);
}

struct gui_download_table wii_download_table = {
	.create = wii_download_create,
	.data = wii_download_data,
	.error = wii_download_error,
	.done = wii_download_done,
};

/* The browser's SD cache runs on the main thread. libfat lacks positional
 * I/O, so preserve the descriptor offset around each cache operation. */
ssize_t pread(int fd, void *buffer, size_t length, off_t offset)
{
	off_t previous = lseek(fd, 0, SEEK_CUR);
	ssize_t result;
	int saved_errno;
	if (previous == (off_t)-1 || lseek(fd, offset, SEEK_SET) == (off_t)-1)
		return -1;
	result = read(fd, buffer, length);
	saved_errno = errno;
	if (lseek(fd, previous, SEEK_SET) == (off_t)-1 && result >= 0)
		return -1;
	errno = saved_errno;
	return result;
}

ssize_t pwrite(int fd, const void *buffer, size_t length, off_t offset)
{
	off_t previous = lseek(fd, 0, SEEK_CUR);
	ssize_t result;
	int saved_errno;
	if (previous == (off_t)-1 || lseek(fd, offset, SEEK_SET) == (off_t)-1)
		return -1;
	result = write(fd, buffer, length);
	saved_errno = errno;
	if (lseek(fd, previous, SEEK_SET) == (off_t)-1 && result >= 0)
		return -1;
	errno = saved_errno;
	return result;
}
