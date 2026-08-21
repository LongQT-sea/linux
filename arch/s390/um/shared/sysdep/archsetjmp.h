/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UML's own jmp_buf for s390x — layout matches setjmp_64.S exactly.
 * Scheduler-jump width (uml_sched_jump_*): callee-saved r6..r15
 * PLUS f8..f15 — s390x gcc uses those FP registers as spill slots
 * for ordinary integer code, so the scheduler switch must record
 * them (the relay-jump pair stores/uses only the GPR fields and
 * leaves the FP fields unspecified, per longjmp.h).
 */
#ifndef __S390_UM_SYSDEP_ARCHSETJMP_H
#define __S390_UM_SYSDEP_ARCHSETJMP_H

struct __jmp_buf {
	unsigned long __r6;
	unsigned long __r7;
	unsigned long __r8;
	unsigned long __r9;
	unsigned long __r10;
	unsigned long __r11;
	unsigned long __r12;
	unsigned long __r13;
	unsigned long __r14;	/* return address */
	unsigned long __r15;	/* stack pointer */
	unsigned long __fprs[8]; /* f8..f15 — scheduler-jump width */
};

typedef struct __jmp_buf jmp_buf[1];

#define JB_IP __r14
#define JB_SP __r15

unsigned long get_thread_reg(int reg, jmp_buf *buf);

#endif /* __S390_UM_SYSDEP_ARCHSETJMP_H */
