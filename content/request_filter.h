/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NETSURF_REQUEST_FILTER_H
#define NETSURF_REQUEST_FILTER_H
#include <stdbool.h>
#include <stdio.h>

/* Load bounded hostname lists. Failure disables filtering and frees all rules.
 * Call on the browser thread before retrievals; finalise after netsurf_exit. */
bool request_filter_init(const char *blocked, const char *allowed);
void request_filter_finalise(void);
bool request_filter_active(void);
/* Match a host and its DNS suffixes. Allow rules and the exact initiating
 * document host take precedence. Inputs are borrowed; this allocates nothing. */
bool request_filter_blocked(const char *host, const char *document_host);
void request_filter_report(FILE *report);
#endif
