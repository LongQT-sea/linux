/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Register/ptrace constants for the s390x UML backend.
 *
 * The syscall number lives in the interruption code, exposed to a
 * tracer as its own regset NT_S390_SYSTEM_CALL (returns nr|0x20000 —
 * mask 0xffff on peek; accepts a bare nr on poke — F-s1). It is
 * mirrored into the dead HOST_SYSCALLNO slot by the marshal; writes to
 * PT_SYSCALL_NR (e.g. the -ERESTARTSYS guard's -1) must never reach a
 * live GPR.
 */
#include <generated/user_constants.h>

#define PT_OFFSET(r) ((r) * sizeof(long))

#define PT_SYSCALL_NR(regs) ((regs)[HOST_SYSCALLNO])
#define PT_SYSCALL_NR_OFFSET PT_OFFSET(HOST_SYSCALLNO)
#define PT_SYSCALL_RET_OFFSET PT_OFFSET(HOST_GPR0 + 2)

#define REGS_IP_INDEX HOST_PSW_ADDR
#define REGS_SP_INDEX HOST_SP

/* s390_fp_regs: fpc + pad + 16 doublewidth FPRs = 136 bytes, fixed.
 * The os-Linux side compiles against libc headers that don't carry
 * struct _s390_fp_regs, so the size is named here for both. */
#define UM_S390_FP_SIZE 136


/*
 * glibc may lag the kernel uapi for SYSEMU on s390 (PTRACE_SYSEMU is
 * arch-generic since Linux 5.3-ish); ensure definitions.
 */
#ifndef PTRACE_SYSEMU
#define PTRACE_SYSEMU 31
#endif
#ifndef PTRACE_SYSEMU_SINGLESTEP
#define PTRACE_SYSEMU_SINGLESTEP 32
#endif
