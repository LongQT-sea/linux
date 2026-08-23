/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thread_struct and cpuinfo_um for the arm64 UML backend: same shape
 * as the x86 UM processor-generic.h, arm64 register model underneath.
 */
#ifndef __UM_ARM64_PROCESSOR_GENERIC_H
#define __UM_ARM64_PROCESSOR_GENERIC_H

struct pt_regs;

struct task_struct;

#include <asm/ptrace.h>
#include <sysdep/archsetjmp.h>

#include <linux/prefetch.h>

struct mm_struct;

struct arch_thread {
	struct faultinfo faultinfo;
};

#define INIT_ARCH_THREAD { .faultinfo = INIT_FAULTINFO }

static inline void arch_copy_thread(struct arch_thread *from,
				    struct arch_thread *to)
{
}

struct thread_struct {
	struct pt_regs *segv_regs;
	struct task_struct *prev_sched;
	struct arch_thread arch;
	jmp_buf switch_buf;
	struct {
		struct {
			int (*proc)(void *);
			void *arg;
		} thread;
	} request;

	void *segv_continue;

	/* Contains variable sized FP registers */
	struct pt_regs regs;
};

#define INIT_THREAD \
{ \
	.regs		   	= EMPTY_REGS,	\
	.prev_sched		= NULL, \
	.arch			= INIT_ARCH_THREAD, \
	.request		= { } \
}

/*
 * User space process size: 3GB (default).
 */
extern unsigned long task_size;

#define TASK_SIZE (task_size)

#undef STACK_TOP
#undef STACK_TOP_MAX

extern unsigned long stacksizelim;

#define STACK_ROOM	(stacksizelim)

#define STACK_TOP	(TASK_SIZE - 2 * PAGE_SIZE)
#define STACK_TOP_MAX	STACK_TOP

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(0x40000000)

extern void start_thread(struct pt_regs *regs, unsigned long entry,
			 unsigned long stack);

struct cpuinfo_um {
	unsigned long loops_per_jiffy;
	int cache_alignment;
	union {
		__u32		arm64_capability[8];
		unsigned long	arm64_capability_alignment;
	};
};

/* Initializer for the backend-owned capability member of cpuinfo_um */
#define CPUINFO_UM_ARCH_INIT	.arm64_capability = { 0 }

extern struct cpuinfo_um boot_cpu_data;

#define cache_line_size()	(boot_cpu_data.cache_alignment)

#define STACKSLOTS_PER_LINE 4

/*
 * Match the x86 UM split exactly: current_sp() is pointer-typed
 * (stacktrace.h uses it in a pointer conditional), current_bp() stays
 * unsigned long (get_frame_pointer() returns it).
 */
#define current_sp() ({ void *_sp; \
			asm volatile("mov %0, sp" : "=r" (_sp)); _sp; })
#define current_bp() ({ unsigned long _bp; \
			asm volatile("mov %0, x29" : "=r" (_bp)); _bp; })

#define KSTK_REG(tsk, reg) get_thread_reg(reg, &tsk->thread.switch_buf)
extern unsigned long __get_wchan(struct task_struct *p);

#endif
