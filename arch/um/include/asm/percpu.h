/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UM_PERCPU_H
#define _ASM_UM_PERCPU_H

#include <asm-generic/percpu.h>

/*
 * The reused x86-64 BPF JIT emits percpu accesses via gs:[this_cpu_off]
 * and takes the symbol's address at compile time. UML has no gs-based
 * percpu addressing; the symbol is provided (by bpf_jit_shim.c) so the
 * JIT compiles and links. Direct-percpu BPF code paths are not exercised
 * by struct_ops/sched_ext; making them actually work under UML is
 * future work.
 *
 * Guarded for assembler use: shared x86 .S files (e.g. clearbhb.S via
 * asm/nospec-branch.h) pull this header into __ASSEMBLY__ builds.
 */
#ifndef __ASSEMBLY__
extern unsigned long this_cpu_off;
#endif

#endif /* _ASM_UM_PERCPU_H */
