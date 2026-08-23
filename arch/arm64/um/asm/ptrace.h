/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_ARM64_PTRACE_H
#define __UM_ARM64_PTRACE_H

#ifndef __ASSEMBLER__

/* Regset indices for the arm64 backend */
enum {
	REGSET_GENERAL,
	REGSET_FP,
};

#include <linux/compiler.h>

/*
 * The arm64 ptrace ABI register frame, owned by the backend (identical
 * to uapi/asm/ptrace.h: x0..x30, sp, pc, pstate), defined here rather
 * than pulled from the native header, which drags in native hwcap.h /
 * cpufeature.h (the CONFIG_ARM64_* class).
 */
struct user_regs_struct {
	__u64 regs[31];
	__u64 sp;
	__u64 pc;
	__u64 pstate;
};

/* fpsimd register frame (identical to uapi/asm/ptrace.h) */
struct user_fpsimd_struct {
	__uint128_t vregs[32];
	__u32 fpsr;
	__u32 fpcr;
	__u32 __reserved[2];
};

#include <asm/ptrace-generic.h>

#define user_mode(r) UPT_IS_USER(&(r)->regs)

/*
 * PSTATE flag and hint bits, as in uapi/asm/ptrace.h. Defined here
 * rather than by including that header: its asm/hwcap.h include
 * resolves to the native arch/arm64 header under this backend's
 * include order and drags in the whole cpufeature apparatus.
 */
#ifndef PSR_SSBS_BIT
#define PSR_SSBS_BIT	0x00001000
#define PSR_DIT_BIT	0x01000000
#define PSR_TCO_BIT	0x02000000
#define PSR_V_BIT	0x10000000
#define PSR_C_BIT	0x20000000
#define PSR_Z_BIT	0x40000000
#define PSR_N_BIT	0x80000000
#endif

/*
 * Bits of PSTATE that a debugger or a sigreturn frame may set,
 * mirroring valid_native_regs() in arch/arm64/kernel/ptrace.c.
 * Anything outside this either names an exception level the guest
 * cannot be at or masks interrupts it does not own; the host would
 * sanitise it on PTRACE_SETREGSET regardless, so filtering here keeps
 * the guest's own view consistent with what will actually be
 * installed.
 */
#define UM_PSTATE_WRITABLE						\
	(PSR_N_BIT | PSR_Z_BIT | PSR_C_BIT | PSR_V_BIT |		\
	 PSR_SSBS_BIT | PSR_DIT_BIT | PSR_TCO_BIT)

/* syscall number in x8, result in x0 */
#define PT_REGS_ORIG_SYSCALL(r) ((r)->regs.gp[HOST_X8])
#define PT_REGS_SYSCALL_RET(r) ((r)->regs.gp[HOST_X0])

/* frame pointer is x29 on arm64 */
#define PT_REGS_BP(r) ((r)->regs.gp[HOST_X29])

#define PT_REGS_PSTATE(r) UPT_PSTATE(&(r)->regs)

#define PT_FIX_EXEC_STACK(sp) do ; while (0)

#define profile_pc(regs) PT_REGS_IP(regs)

/*
 * Restart a syscall: rewind the PC by one instruction (svc is 4
 * bytes) and restore x0 from the trap-time snapshot (HOST_ARG0), as
 * native arm64 restores orig_x0. By restart time x0 holds the
 * -ERESTART* error, not the first argument.
 */
#define UPT_RESTART_SYSCALL(r) do {					\
	(r)->gp[HOST_X0] = (r)->gp[HOST_ARG0];				\
	UPT_IP(r) -= 4;							\
} while (0)
#define PT_REGS_SET_SYSCALL_RETURN(r, res) (PT_REGS_SYSCALL_RET(r) = (res))

static inline long regs_return_value(struct pt_regs *regs)
{
	return PT_REGS_SYSCALL_RET(regs);
}

#define user_stack_pointer(regs) PT_REGS_SP(regs)

struct task_struct;
extern void arch_switch_to(struct task_struct *to);

#endif /* __ASSEMBLER__ */

#endif /* __UM_ARM64_PTRACE_H */
