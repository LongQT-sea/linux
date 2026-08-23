// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Richard Weinberger <richrd@nod.at>
 *
 * This vDSO turns all calls into a syscall so that UML can trap them.
 * (arm64 version: svc #0 with x8 = nr, x0..x5 = args.)
 */

/* Disable profiling for userspace code */
#define DISABLE_BRANCH_PROFILING

#include <vdso/gettime.h>
#include <linux/time.h>
#include <asm/unistd.h>

int __vdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts)
{
	register long x8 __asm__("x8") = __NR_clock_gettime;
	register long x0 __asm__("x0") = (long)clock;
	register long x1 __asm__("x1") = (long)ts;

	asm volatile("svc #0"
		: "+r" (x0)
		: "r" (x1), "r" (x8)
		: "memory");

	return x0;
}
int clock_gettime(clockid_t, struct __kernel_timespec *)
	__attribute__((weak, alias("__vdso_clock_gettime")));

int __vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz)
{
	register long x8 __asm__("x8") = __NR_gettimeofday;
	register long x0 __asm__("x0") = (long)tv;
	register long x1 __asm__("x1") = (long)tz;

	asm volatile("svc #0"
		: "+r" (x0)
		: "r" (x1), "r" (x8)
		: "memory");

	return x0;
}
int gettimeofday(struct __kernel_old_timeval *, struct timezone *)
	__attribute__((weak, alias("__vdso_gettimeofday")));

/*
 * __kernel_rt_sigreturn, the signal-return trampoline the guest kernel
 * points x30 at when delivering a signal (arm64 has no SA_RESTORER),
 * lives in sigreturn.S as real assembly, because aarch64 GCC may
 * ignore the naked attribute and emit a sigframe-clobbering prologue.
 */
