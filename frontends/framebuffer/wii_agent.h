/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NETSURF_WII_AGENT_H
#define NETSURF_WII_AGENT_H
#include <stdbool.h>
void wii_agent_stage(const char *stage);
void wii_agent_begin_shutdown(void);
void wii_agent_network_ready(int result);
bool wii_agent_poll(void);
void wii_agent_arm_crash(void);
void wii_agent_arm_shutdown_test(void);
void wii_agent_arm_memory_test(void);
unsigned wii_agent_heap_gap(void);
#ifdef NETSURF_HBC_AGENT_CRASH_TEST
void wii_agent_test_crash(void);
#endif
#endif
