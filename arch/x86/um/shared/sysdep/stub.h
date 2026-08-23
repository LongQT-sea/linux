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

extern void stub_segv_handler(int, siginfo_t *, void *);
extern void stub_syscall_handler(void);
extern void stub_signal_interrupt(int, siginfo_t *, void *);
extern void stub_signal_restorer(void);

#endif /* __SYSDEP_X86_STUB_H */
