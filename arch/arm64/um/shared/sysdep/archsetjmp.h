/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UML's own jmp_buf for arm64; the layout matches setjmp_64.S exactly:
 * x19..x28, fp (x29), lr (x30), sp, pc.
 */
#ifndef __ARM64_UM_SYSDEP_ARCHSETJMP_H
#define __ARM64_UM_SYSDEP_ARCHSETJMP_H

struct __jmp_buf {
	unsigned long __x19;
	unsigned long __x20;
	unsigned long __x21;
	unsigned long __x22;
	unsigned long __x23;
	unsigned long __x24;
	unsigned long __x25;
	unsigned long __x26;
	unsigned long __x27;
	unsigned long __x28;
	unsigned long __fp;
	unsigned long __lr;
	unsigned long __sp;
	unsigned long __pc;
};

typedef struct __jmp_buf jmp_buf[1];

#define JB_IP __pc
#define JB_SP __sp

unsigned long get_thread_reg(int reg, jmp_buf *buf);

#endif /* __ARM64_UM_SYSDEP_ARCHSETJMP_H */
