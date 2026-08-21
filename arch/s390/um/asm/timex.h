/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UML/s390x timex shim. Mainline removed the generic arch/um
 * asm/timex.h shim (commit 3ed403bbc967 "treewide: Remove
 * CLOCK_TICK_RATE"), which let <linux/timex.h> fall through to the
 * native s390 timex.h; its #include <asm/lowcore.h> requires psw_t,
 * undefined in UML translation units where asm/ptrace.h resolves to
 * this backend's shadow headers. Keep the pre-removal behavior:
 * provide the trivial definitions UML needs and use the asm-generic
 * version instead of any native s390 header.
 */
#ifndef __UM_S390_TIMEX_H
#define __UM_S390_TIMEX_H

#define CLOCK_TICK_RATE (HZ)

#include <asm-generic/timex.h>

#endif
