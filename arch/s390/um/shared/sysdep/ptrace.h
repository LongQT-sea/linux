/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Register model for the s390x UML backend.
 *
 * There is no single pc/pstate: the PSW is a pair — mask (flags/mode
 * bits) + addr (instruction address == PC). The syscall number is not
 * in a GPR: it lives in the interruption code (`int_code`, low 16
 * bits), reachable from the tracer via the NT_S390_SYSTEM_CALL regset
 * and carried into gp as the dead HOST_SYSCALLNO slot by the marshal.
 * Args r2..r6 (+r7 for the 6th); return in r2; entry r2 preserved in
 * orig_gpr2. TLS rides acrs[0..1] big-endian hi/lo.
 */
#ifndef __SYSDEP_S390_PTRACE_H
#define __SYSDEP_S390_PTRACE_H

#include <generated/user_constants.h>
#include <asm/types.h>			/* __vector128 for fpu chains */
#include <sysdep/faultinfo.h>

#define MAX_REG_OFFSET (UM_FRAME_SIZE)
#define MAX_REG_NR ((MAX_REG_OFFSET) / sizeof(unsigned long))

/*
 * Full gp-frame size in unsigned longs: the ABI frame plus the
 * UML-internal slots below. Shared-core gp[] buffers size off this,
 * never MAX_REG_NR + N. [seam commit 4197af5af364]
 */
#define UM_GP_SLOTS (MAX_REG_NR + 3)

/*
 * UML-internal slot past the ABI frame (psw, gprs, acrs, orig_gpr2):
 * the syscall number, mirrored from int_code by the regset marshal /
 * get_stub_state. PT_SYSCALL_NR must not alias a live GPR — on s390 no
 * GPR is dead storage, so this synthetic slot is mandatory.
 */
#define HOST_SYSCALLNO (MAX_REG_NR)

/* Second internal slot: trap-time r2 snapshot. handle_syscall writes
 * -ENOSYS into gprs[2] BEFORE reading args; r2 is both return and
 * arg1, so UPT_SYSCALL_ARG1 reads orig_gpr2 instead — but the seccomp
 * relay needs the pre-clobber r2 for set_stub_state restarts. */
#define HOST_ARG0 (MAX_REG_NR + 1)

/* Third internal slot: guest TLS pointer assembled from acrs[0..1]
 * (big-endian hi/lo) by the marshal, mirroring arm64's HOST_TLS role.
 * F-s1 verified these survive traps via NT_PRSTATUS and mcontext. */
#define HOST_TLS (MAX_REG_NR + 2)

/* Register accessors over the gp frame */
#define REGS_PSW_MASK(r) ((r)[HOST_PSW_MASK])
#define REGS_PSW_ADDR(r) ((r)[HOST_PSW_ADDR])
#define REGS_GPR(r, n) ((r)[HOST_GPR0 + (n)])
#define REGS_SP(r) ((r)[HOST_SP])

#define UPT_IP(r) REGS_PSW_ADDR((r)->gp)
#define UPT_SP(r) REGS_SP((r)->gp)
#define UPT_PSTATE(r) REGS_PSW_MASK((r)->gp)

/* Assemble the 64-bit TLS pointer from the 32-bit acrs halves,
 * explicitly big-endian hi/lo (acrs[0]=tls>>32, acrs[1]=(u32)tls). */
static __always_inline unsigned long
upt_acrs_tls(const unsigned long *gp)
{
	unsigned int hi = (unsigned int)(gp[HOST_ACRS] >> 32);
	unsigned int lo = (unsigned int)(gp[HOST_ACRS] & 0xffffffffUL);

	return ((unsigned long)hi << 32) | lo;
}

#define UPT_SYSCALL_ARG1(r) ((r)->gp[HOST_ORIG_GPR2])
#define UPT_SYSCALL_ARG2(r) ((r)->gp[HOST_GPR0 + 3])
#define UPT_SYSCALL_ARG3(r) ((r)->gp[HOST_GPR0 + 4])
#define UPT_SYSCALL_ARG4(r) ((r)->gp[HOST_GPR0 + 5])
#define UPT_SYSCALL_ARG5(r) ((r)->gp[HOST_GPR0 + 6])
#define UPT_SYSCALL_ARG6(r) ((r)->gp[HOST_GPR0 + 7])

extern unsigned long host_fp_size;

struct uml_pt_regs {
	unsigned long gp[UM_GP_SLOTS]; /* ABI frame + SYSCALLNO/ARG0/TLS */
	struct faultinfo faultinfo;
	long syscall;
	int is_user;

	/* Dynamically sized FP registers (fixed 136B s390_fp_regs in v0;
	 * NT_S390_VXRS_* vector state skipped, like arm64-no-SVE) */
	unsigned long fp[];
};

#define EMPTY_UML_PT_REGS { }

#define UPT_SYSCALL_NR(r) ((r)->syscall)
#define UPT_FAULTINFO(r) (&(r)->faultinfo)
#define UPT_IS_USER(r) ((r)->is_user)

extern int arch_init_registers(int pid);

/* Initial stack pointer for a new thread. The s390x ELF ABI gives the
 * callee a 160-byte register-save area ABOVE its entry SP (the prologue
 * `stmg %r14,%r15,112(%r15)` runs BEFORE the stack decrement, storing
 * into the caller-provided area). At thread birth there is no caller,
 * so the initial SP must reserve that headroom below the stack top or
 * the first stmg writes past the allocation (SIGILL/SIGSEGV at
 * UM_THREAD_START_SP+112 — box-proven). 16-byte aligned like siblings. */
#define UM_THREAD_START_SP(stack) \
	(((unsigned long)(stack) + UM_THREAD_SIZE - 176) & ~15UL)

#endif /* __SYSDEP_S390_PTRACE_H */
