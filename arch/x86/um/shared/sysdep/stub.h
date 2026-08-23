/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Guarded because <stub-futex.h> includes this on top of direct includers.
 * The guard name must differ from __SYSDEP_STUB_H: stub_32.h/stub_64.h below
 * already use that one internally, and defining it here first would silently
 * skip their entire body.
 */
#ifndef __SYSDEP_X86_STUB_H
#define __SYSDEP_X86_STUB_H

#include <asm/unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <time.h>
#include <cpuid.h>
#include <as-layout.h>
#include <stub-data.h>

#ifdef __i386__
#include "stub_32.h"
#else
#include "stub_64.h"
#endif

/*
 * No handoff-word atomics here: x86-TSO makes the generic compiler-builtin
 * fallbacks in <stub-futex.h> already optimal (the acquire load is a plain
 * MOV plus a compiler barrier; the read-modify-writes need their LOCK prefix
 * for atomicity regardless), and the builtins inline, so the stub stays
 * call-closed. This backend is the in-tree proof that the generic fallback
 * path works.
 */

/* Nothing this architecture needs to settle in a fresh stub. */
static __always_inline void stub_arch_init(unsigned long arch_flags)
{
}

/* Spin-hint and cycle-counter hooks for the stub handoff (<stub-futex.h>). */
static __always_inline void stub_relax(void)
{
	/* PAUSE, encoded so pre-SSE2 assemblers accept it too. */
	__asm__ volatile("rep; nop" ::: "memory");
}
#define stub_relax stub_relax

static __always_inline unsigned long long stub_rdtsc(void)
{
	unsigned int lo, hi;

	__asm__ volatile("rdtsc" : "=a" (lo), "=d" (hi));

	return ((unsigned long long)hi << 32) | lo;
}

/*
 * On 32-bit the truncation to unsigned long keeps only the TSC's low word;
 * the spin loop's unsigned wraparound arithmetic copes, and a budget is
 * bounded well below 2^32 ticks by the seccomp_spin= clamp.
 */
static __always_inline unsigned long stub_cycles(void)
{
	return stub_rdtsc();
}
#define stub_cycles stub_cycles

/*
 * Only ever called on the host side (never from the stub): the rate check
 * and calibration below use CPUID and the host clock, which freestanding
 * stub code has no business touching. check_stub_cycles() runs this once at
 * boot and caches the result.
 */
static inline unsigned long stub_cycles_per_us(void)
{
	unsigned int eax, ebx, ecx, edx;
	unsigned long long tsc0, tsc1;
	struct timespec t0, t1;
	long long ns;

	/*
	 * Invariant TSC (CPUID.80000007H:EDX[8]): constant rate regardless
	 * of the governor, and not stopped in deep C-states. Without it the
	 * TSC follows the CPU clock and cannot express a wall-time budget;
	 * report "no usable rate", which disables the spin.
	 */
	if (__get_cpuid_max(0x80000000, NULL) < 0x80000007)
		return 0;
	__cpuid(0x80000007, eax, ebx, ecx, edx);
	if (!(edx & (1 << 8)))
		return 0;

	/*
	 * The invariant-TSC rate is not architecturally enumerable on every
	 * CPU that has one; calibrate against the host clock instead. Two
	 * milliseconds bound the error far below anything a spin budget
	 * cares about.
	 */
	if (clock_gettime(CLOCK_MONOTONIC, &t0))
		return 0;
	tsc0 = stub_rdtsc();
	do {
		if (clock_gettime(CLOCK_MONOTONIC, &t1))
			return 0;
		ns = (t1.tv_sec - t0.tv_sec) * 1000000000LL +
		     (t1.tv_nsec - t0.tv_nsec);
	} while (ns < 2000000);
	tsc1 = stub_rdtsc();

	return (tsc1 - tsc0) * 1000 / ns;
}

extern void stub_segv_handler(int, siginfo_t *, void *);
extern void stub_syscall_handler(void);
extern void stub_signal_interrupt(int, siginfo_t *, void *);
extern void stub_signal_restorer(void);

#endif /* __SYSDEP_X86_STUB_H */
