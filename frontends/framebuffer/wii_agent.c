/* SPDX-License-Identifier: GPL-2.0-only */
/* Optional HBC-Reborn development service. Ordinary builds have no agent. */
#include "framebuffer/wii_agent.h"
#ifdef NETSURF_HBC_AGENT
#include <gccore.h>
#include <hbc_agent.h>
#include <hbc_netlog.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>
#include "desktop/version.h"

/* Development-only escape from a blocked main loop or teardown.  _Exit
 * bypasses libc/SDL cleanup that may be the operation which is wedged;
 * libogc's exit syscall still reloads HBC through its installed stub. */
static volatile u32 progress_ms, progress_limit_ms = 60000;
static bool watchdog_started;
static lwp_t watchdog_thread;
static unsigned char watchdog_stack[8192] __attribute__((aligned(32)));
static u32 monotonic_ms(void)
{
	return (u32)ticks_to_millisecs(gettime());
}
static void *watch_progress(void *unused)
{
	(void)unused;
	for (;;) {
		usleep(1000000);
		if ((u32)(monotonic_ms() - progress_ms) >= progress_limit_ms)
			_Exit(EXIT_FAILURE);
	}
	return NULL;
}

void wii_agent_stage(const char *stage)
{
	progress_ms = monotonic_ms();
	if (!watchdog_started) {
		if (LWP_CreateThread(&watchdog_thread,
				     watch_progress,
				     NULL,
				     watchdog_stack,
				     sizeof(watchdog_stack),
				     96) != 0)
			_Exit(EXIT_FAILURE);
		watchdog_started = true;
	}
	FILE *file = fopen("sd:/apps/netsurf/wii-lifecycle.txt", "w");
	if (file) {
		fprintf(file, "stage=%s\n", stage);
		fclose(file);
	}
}

void wii_agent_begin_shutdown(void)
{
	progress_ms = monotonic_ms();
	progress_limit_ms = 10000;
}

static void agent_fallback_exit(void *unused)
{
	(void)unused;
	_Exit(EXIT_FAILURE);
}

static volatile bool network_ready;
static bool started, shutdown_test_armed;
void wii_agent_arm_shutdown_test(void)
{
	shutdown_test_armed = true;
}
#ifdef NETSURF_HBC_AGENT_CRASH_TEST
static bool crash_armed;
/* Deliberately outside RAM, only in explicitly instrumented probe builds. */
__attribute__((noinline)) void wii_agent_test_crash(void)
{
	*(volatile u32 *)0x10 = 0x4e535746;
}
void wii_agent_arm_crash(void)
{
	crash_armed = true;
}
#else
void wii_agent_arm_crash(void)
{
}
#endif

/* Reserve the ordinary malloc chunk containing HBC's persistent records.
 * Freeing the other chunks lets newlib reuse MEM1 and both sides of MEM2;
 * SDK arena highs (and IOS reservations) are never changed. */
#define AGENT_RESERVATION_SIZE (1024u * 1024u)
#define AGENT_RESERVATION_SLOTS 64
_Static_assert(HBC_NETLOG_KEEP_ADDR >= (HBC_CRASH_ADDR & ~4095u) &&
		       HBC_NETLOG_KEEP_ADDR + sizeof(hbc_netlog_block) <=
			       (HBC_CRASH_ADDR & ~4095u) + 4096u &&
		       HBC_CRASH_ADDR + sizeof(hbc_crash_block) <=
			       (HBC_CRASH_ADDR & ~4095u) + 4096u,
	       "HBC persistent records must fit the retained page");
static void *record_allocation;
static unsigned heap_gap;
unsigned wii_agent_heap_gap(void)
{
	return heap_gap;
}

__attribute__((constructor)) static void reserve_agent_records(void)
{
	void *temporary[AGENT_RESERVATION_SLOTS];
	unsigned count = 0;
	uintptr_t first = HBC_CRASH_ADDR & ~(uintptr_t)4095;
	uintptr_t last = first + 4096;
	while (count < AGENT_RESERVATION_SLOTS) {
		uintptr_t mem1_end = (uintptr_t)SYS_GetArena1Lo();
		uintptr_t mem2_begin = (uintptr_t)SYS_GetArena2Lo();
		void *allocation = malloc(AGENT_RESERVATION_SIZE);
		/* newlib counts this discontinuity as foreign sbrk consumption.
		 */
		if (heap_gap == 0 && (uintptr_t)SYS_GetArena2Lo() > mem2_begin)
			heap_gap = (unsigned)(mem2_begin - mem1_end);
		if (allocation == NULL)
			break;
		uintptr_t address = (uintptr_t)allocation;
		if (address <= first &&
		    address + AGENT_RESERVATION_SIZE >= last) {
			record_allocation = allocation;
			break;
		}
		temporary[count++] = allocation;
		/* A record in an allocation header cannot safely be retained.
		 */
		if (address > first)
			break;
	}
	while (count != 0)
		free(temporary[--count]);
	if (record_allocation == NULL) {
		SYS_Report(
			"NetSurf: HBC record reservation failed; refusing to boot\n");
		_Exit(EXIT_FAILURE);
	}
	SYS_Report(
		"NetSurf: HBC records protected by %p (%u bytes), MEM2 high=%p\n",
		record_allocation,
		AGENT_RESERVATION_SIZE,
		SYS_GetArena2Hi());
}

static bool memory_test_armed;
void wii_agent_arm_memory_test(void)
{
	memory_test_armed = true;
}

/* Explicit development diagnostic: exercise simultaneously allocated MEM2,
 * including every 4 KiB page, then release it before normal browser work. */
static void test_mem2_capacity(void)
{
	void *blocks[64];
	unsigned count = 0, mem2_blocks = 0;
	bool valid = true;
	unsigned char records[4096];
	uintptr_t first = HBC_CRASH_ADDR & ~(uintptr_t)4095;
	uintptr_t high = (uintptr_t)SYS_GetArena2Hi();
	memcpy(records, (void *)first, sizeof(records));
	while (count < 64) {
		unsigned *block = malloc(AGENT_RESERVATION_SIZE);
		if (block == NULL)
			break;
		blocks[count++] = block;
		uintptr_t address = (uintptr_t)block;
		if (address >= 0x90000000u && address < 0x94000000u) {
			mem2_blocks++;
			if (address + AGENT_RESERVATION_SIZE > high ||
			    (address < first + sizeof(records) &&
			     address + AGENT_RESERVATION_SIZE > first))
				valid = false;
		}
		/* Do not write an invalid allocation even if the allocator
		 * regresses. */
		if (!valid)
			break;
		for (unsigned i = 0;
		     i < AGENT_RESERVATION_SIZE / sizeof(*block);
		     i += 1024)
			block[i] = (unsigned)address ^ i;
	}
	for (unsigned n = 0; n < count && valid; n++) {
		unsigned *block = blocks[n];
		for (unsigned i = 0;
		     i < AGENT_RESERVATION_SIZE / sizeof(*block);
		     i += 1024)
			if (block[i] != ((unsigned)(uintptr_t)block ^ i))
				valid = false;
	}
	while (count != 0)
		free(blocks[--count]);
	valid = valid && memcmp(records, (void *)first, sizeof(records)) == 0;
	FILE *report = fopen("sd:/apps/netsurf/mem2-test.txt", "w");
	if (report != NULL) {
		fprintf(report,
			"mem2_test=%s\nallocated_mem2=%u\nreserved=%u\nMEM2_high=%p\n",
			valid && mem2_blocks >= 50 ? "PASS" : "FAIL",
			mem2_blocks * AGENT_RESERVATION_SIZE,
			AGENT_RESERVATION_SIZE,
			(void *)high);
		fclose(report);
	}
	SYS_Report(
		"NetSurf: MEM2 capacity test %s allocated=%u reserved=%u high=%p\n",
		valid && mem2_blocks >= 50 ? "PASS" : "FAIL",
		mem2_blocks * AGENT_RESERVATION_SIZE,
		AGENT_RESERVATION_SIZE,
		(void *)high);
}

void wii_agent_network_ready(int result)
{
	network_ready = result >= 0;
}

bool wii_agent_poll(void)
{
	progress_ms = monotonic_ms();
	if (!started && network_ready) {
		hbc_agent_config cfg = {0};
		cfg.name = "NetSurf Wii";
		cfg.version = netsurf_version;
		cfg.priority = 40;
		cfg.app_polls_exit = true;
		cfg.exit_grace_ms = 10000;
		cfg.on_exit = agent_fallback_exit;
		cfg.crash_reload_s = 3;
		/* SDL owns controllers/GX. Do not launch the agent's HOME
		 * overlay. */
		int result = hbc_agent_init(&cfg);
		SYS_Report("NetSurf: HBC agent init=%d MEM2 high=%p\n",
			   result,
			   SYS_GetArena2Hi());
		if (result < 0)
			_Exit(EXIT_FAILURE);
		started = true;
		/* Connect only to a logging target explicitly registered
		 * through HBC. */
		int log_result = hbc_netlog_init();
		FILE *log_status = fopen("sd:/apps/netsurf/agent-log-status.txt", "w");
		if (log_status != NULL) {
			fprintf(log_status, "netlog_init=%d\n", log_result);
			fclose(log_status);
		}
		if (log_result == 0)
			fprintf(stderr, "NetSurf: agent log connected\n");
	}
	if (started && shutdown_test_armed) {
		wii_agent_begin_shutdown();
		wii_agent_stage("shutdown stall probe");
		for (;;)
			usleep(1000000); /* opt-in: the watchdog must return to
					    HBC */
	}
	if (started && memory_test_armed) {
		memory_test_armed = false;
		test_mem2_capacity();
	}
#ifdef NETSURF_HBC_AGENT_CRASH_TEST
	if (started && crash_armed)
		wii_agent_test_crash();
#endif
	return started && hbc_agent_exit_requested();
}
#else
void wii_agent_stage(const char *stage)
{
	(void)stage;
}
void wii_agent_begin_shutdown(void)
{
}
unsigned wii_agent_heap_gap(void)
{
	return 0;
}
void wii_agent_arm_crash(void)
{
}
void wii_agent_arm_shutdown_test(void)
{
}
void wii_agent_arm_memory_test(void)
{
}
void wii_agent_network_ready(int result)
{
	(void)result;
}
bool wii_agent_poll(void)
{
	return false;
}
#endif
