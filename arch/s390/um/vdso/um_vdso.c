// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Richard Weinberger <richrd@nod.at>
 *
 * This vDSO turns all calls into a syscall so that UML can trap them.
 * (s390 version: svc 0 with the number in r1, result r2 — same shape
 * as arch/s390's GENERATE_SYSCALL_FUNC.)
 */

/* Disable profiling for userspace code */
#define DISABLE_BRANCH_PROFILING

#include <vdso/gettime.h>
#include <linux/time.h>
#include <asm/unistd.h>

static __always_inline long vdso_syscall2(long nr, long a1, long a2)
{
	register long r1 __asm__("1") = nr;
	register long r2 __asm__("2") = a1;
	register long r3 __asm__("3") = a2;

	asm volatile(
		"	svc	0\n"
		: "+d" (r2)			/* svc returns in r2 */
		: "d" (r1), "d" (r3)
		: "memory", "cc");

	return r2;
}

int __vdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts)
{
	return (int)vdso_syscall2(__NR_clock_gettime, clock, (long)ts);
}
int clock_gettime(clockid_t, struct __kernel_timespec *)
	__attribute__((weak, alias("__vdso_clock_gettime")));

int __vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz)
{
	return (int)vdso_syscall2(__NR_gettimeofday, (long)tv, (long)tz);
}
int gettimeofday(struct __kernel_old_timeval *, struct timezone *)
	__attribute__((weak, alias("__vdso_gettimeofday")));

/*
 * __kernel_rt_sigreturn, the signal-return trampoline the guest kernel
 * points gprs[14] at when delivering a signal (s390 SA_RESTORER
 * convention), lives in sigreturn.S — real assembly, because a C
 * function may emit a sigframe-clobbering prologue.
 */
