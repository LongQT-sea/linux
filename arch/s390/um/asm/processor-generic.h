/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thread_struct and cpuinfo_um for the s390x UML backend — same shape
 * as the x86/arm64 UM processor-generic.h, s390 register model
 * underneath.
 */
#ifndef __UM_S390_PROCESSOR_GENERIC_H
#define __UM_S390_PROCESSOR_GENERIC_H

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

static inline void arch_flush_thread(struct arch_thread *thread)
{
}

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
		unsigned long	s390_facility[4];
		unsigned long	s390_facility_alignment;
	};
};

/* Initializer for the backend-owned capability member of cpuinfo_um */
#define CPUINFO_UM_ARCH_INIT	.s390_facility = { 0 }

extern struct cpuinfo_um boot_cpu_data;

#define cache_line_size()	(boot_cpu_data.cache_alignment)

#define STACKSLOTS_PER_LINE 4

#define current_sp() ({ void *_sp; \
			asm volatile("lr %%r15,%0" : "=r" (_sp)); _sp; })
#define current_bp() ({ unsigned long _bp; \
			asm volatile("lr %%r11,%0" : "=r" (_bp)); _bp; })

#define KSTK_REG(tsk, reg) get_thread_reg(reg, &tsk->thread.switch_buf)
extern unsigned long __get_wchan(struct task_struct *p);

#endif
