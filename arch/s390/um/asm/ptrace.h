/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_S390_PTRACE_H
#define __UM_S390_PTRACE_H

#ifndef __ASSEMBLER__

/* Regset indices for the s390x backend */
enum {
	REGSET_GENERAL,
	REGSET_FP,
};

#include <linux/compiler.h>

/*
 * The s390 ptrace ABI register frame, owned by the backend (identical
 * to uapi/asm/ptrace.h's s390_regs: psw, gprs, acrs, orig_gpr2 —
 * box-verified 216 bytes, F-s1). Defined here rather than pulled from
 * the native header to keep the UML surface explicit; the trailing
 * per_struct/fp_regs of user_regs_struct are NOT part of the regset
 * (they are PEEKUSER-space only).
 */
struct user_regs_struct {
	struct {
		unsigned long mask;
		unsigned long addr;
	} __attribute__((aligned(8))) psw;
	unsigned long gprs[16];
	unsigned int acrs[16];
	unsigned long orig_gpr2;
};

/* Fixed 136-byte FP frame (fpc + pad + 16 FPRs), uapi shape */
struct user_fpregs_struct {
	unsigned int fpc;
	unsigned int pad;
	union {
		double d;
		float f;
		unsigned long long ui;
	} fprs[16];
};

#include <asm/ptrace-generic.h>

/*
 * Host-lane PER single-stepping dies at the host sigreturn in
 * seccomp mode (the host merge drops the PER bit); arch_ptrace must
 * refuse PTRACE_SINGLESTEP rather than fake syscall-granularity
 * steps via the syscall-leave trap. Breakpoints via the 0x5000-area
 * requests still work.
 */
#define SUBARCH_SINGLESTEP_UNSUPPORTED

#define user_mode(r) UPT_IS_USER(&(r)->regs)

/*
 * Syscall number lives in int_code, mirrored into the dead
 * HOST_SYSCALLNO slot by the marshal. Result in r2. The entry r2 is
 * preserved in orig_gpr2 by the ABI.
 */
#define PT_REGS_ORIG_SYSCALL(r) ((r)->regs.gp[HOST_SYSCALLNO])
#define PT_REGS_SYSCALL_RET(r) ((r)->regs.gp[HOST_GPR0 + 2])

/* frame pointer convention on s390 is r11 in the kernel */
#define PT_REGS_BP(r) ((r)->regs.gp[HOST_GPR0 + 11])

#define PT_REGS_PSTATE(r) UPT_PSTATE(&(r)->regs)

#define PT_FIX_EXEC_STACK(sp) do ; while(0)

#define profile_pc(regs) PT_REGS_IP(regs)

/*
 * svc is 2 bytes — rewind one instruction to restart a syscall.
 * (The kernel-side restart actually re-writes int_code via the
 * marshal; rewinding IP keeps the non-seccomp SYSEMU path honest.)
 */
#define UPT_RESTART_SYSCALL(r) (UPT_IP(r) -= 2)
#define PT_REGS_SET_SYSCALL_RETURN(r, res) (PT_REGS_SYSCALL_RET(r) = (res))

static inline long regs_return_value(struct pt_regs *regs)
{
	return PT_REGS_SYSCALL_RET(regs);
}

#define user_stack_pointer(regs) PT_REGS_SP(regs)

struct task_struct;
extern void arch_switch_to(struct task_struct *to);

#endif /* __ASSEMBLER__ */

#endif /* __UM_S390_PTRACE_H */
