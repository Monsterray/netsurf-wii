/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native hostname-filter plugin. No scripts, regex engine or runtime
 * allocation. */
#include "content/request_filter.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

struct rules {
	char *text;
	char **hosts;
	size_t count, bytes;
};
static struct rules deny, allow;
static unsigned long checked, blocked;
static bool enabled;
bool request_filter_active(void)
{
	return enabled;
}

static int compare(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static bool hostname(char *name)
{
	size_t length = strlen(name), label = 0;
	if (length && name[length - 1] == '.')
		name[--length] = 0;
	if (!length || length > 253)
		return false;
	for (size_t i = 0; i < length; i++) {
		unsigned char ch = (unsigned char)name[i];
		if (ch == '.') {
			if (!label || label > 63 || name[i - 1] == '-')
				return false;
			label = 0;
		} else {
			if (!((ch >= 'a' && ch <= 'z') ||
			      (ch >= 'A' && ch <= 'Z') ||
			      (ch >= '0' && ch <= '9') || (ch == '-' && label)))
				return false;
			name[i] = (char)tolower(ch);
			label++;
		}
	}
	return label && label <= 63 && name[length - 1] != '-';
}

static bool
load(struct rules *rules, const char *path, size_t budget, size_t limit)
{
	FILE *file = fopen(path, "rb");
	long length;
	if (!file)
		return false;
	if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 ||
	    (size_t)length > budget || fseek(file, 0, SEEK_SET)) {
		fclose(file);
		return false;
	}
	rules->text = malloc((size_t)length + 1);
	rules->hosts = calloc(limit, sizeof(char *));
	if (!rules->text || !rules->hosts) {
		fclose(file);
		return false;
	}
	rules->bytes = (size_t)length + 1 + limit * sizeof(char *);
	if (fread(rules->text, 1, length, file) != (size_t)length) {
		fclose(file);
		return false;
	}
	fclose(file);
	rules->text[length] = 0;
	/* Embedded NULs could silently discard the rest of a supplied policy.
	 */
	if (memchr(rules->text, 0, length))
		return false;
	char *line = rules->text;
	while (*line) {
		char *next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		char *comment = strchr(line, '#');
		if (comment)
			*comment = 0;
		while (isspace((unsigned char)*line))
			line++;
		char *end = line + strlen(line);
		while (end > line && isspace((unsigned char)end[-1]))
			*--end = 0;
		if (*line) {
			/* This format is deliberately hostname-only;
			 * unsupported ABP, hosts-file and cosmetic syntax fails
			 * rather than overblocks. */
			if (!hostname(line) || rules->count == limit)
				return false;
			rules->hosts[rules->count++] = line;
		}
		if (!next)
			break;
		line = next;
	}
	qsort(rules->hosts, rules->count, sizeof(char *), compare);
	return true;
}

void request_filter_finalise(void)
{
	free(deny.text);
	free(deny.hosts);
	free(allow.text);
	free(allow.hosts);
	memset(&deny, 0, sizeof(deny));
	memset(&allow, 0, sizeof(allow));
	enabled = false;
	checked = blocked = 0;
}

bool request_filter_init(const char *denied, const char *allowed)
{
	request_filter_finalise();
	if (!load(&deny, denied, 256 * 1024, 4096) ||
	    !load(&allow, allowed, 32 * 1024, 512)) {
		request_filter_finalise();
		return false;
	}
	enabled = true;
	return true;
}

static bool matches(const struct rules *rules, const char *host)
{
	do {
		if (bsearch(&host,
			    rules->hosts,
			    rules->count,
			    sizeof(char *),
			    compare))
			return true;
		host = strchr(host, '.');
		if (host)
			host++;
	} while (host && *host);
	return false;
}

bool request_filter_blocked(const char *host, const char *document_host)
{
	char canonical[254], document[254];
	if (!enabled || !host || strlen(host) >= sizeof(canonical))
		return false;
	checked++;
	strcpy(canonical, host);
	if (!hostname(canonical) || matches(&allow, canonical))
		return false;
	if (document_host && strlen(document_host) < sizeof(document)) {
		strcpy(document, document_host);
		if (hostname(document) && strcmp(canonical, document) == 0)
			return false;
	}
	if (!matches(&deny, canonical))
		return false;
	blocked++;
	return true;
}

void request_filter_report(FILE *report)
{
	fprintf(report,
		"request_filter=%s\nfilter_rules=%zu\nfilter_bytes=%zu\nfilter_checked=%lu\nfilter_blocked=%lu\n",
		enabled ? "hosts" : "off",
		deny.count + allow.count,
		deny.bytes + allow.bytes,
		checked,
		blocked);
}
