// SPDX-License-Identifier: GPL-2.0-only
/*
 * Delay loops for the s390x UML backend, mirroring
 * arch/x86/um/delay.c's structure with an s390 branch-on-count loop.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm/param.h>

void __delay(unsigned long loops)
{
	asm volatile(
		"0:	brct	%0, 0b\n"
		: "+d" (loops) :: "cc");
}
EXPORT_SYMBOL(__delay);

inline void __const_udelay(unsigned long xloops)
{
	__delay(((xloops * HZ * loops_per_jiffy) >> 32) + 1);
}
EXPORT_SYMBOL(__const_udelay);

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x10c7UL); /* 2**32 / 1000000 (rounded up) */
}
EXPORT_SYMBOL(__udelay);
