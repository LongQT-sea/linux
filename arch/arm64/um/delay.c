// SPDX-License-Identifier: GPL-2.0-only
/*
 * Delay loops for the arm64 UML backend, mirroring arch/x86/um/delay.c's
 * structure with an arm64 loop.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm/param.h>

void __delay(unsigned long loops)
{
	asm volatile(
		"subs	%0, %0, #1\n"
		"bne	.-4\n"
		: "+r" (loops)
	);
}
EXPORT_SYMBOL(__delay);

inline void __const_udelay(unsigned long xloops)
{
	xloops = (xloops * 4ULL * (loops_per_jiffy * (HZ / 4))) >> 32;

	__delay(++xloops);
}
EXPORT_SYMBOL(__const_udelay);

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c7); /* 2**32 / 1000000 (rounded up) */
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x00005); /* 2**32 / 1000000000 (rounded up) */
}
EXPORT_SYMBOL(__ndelay);
