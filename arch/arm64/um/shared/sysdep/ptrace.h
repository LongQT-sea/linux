/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Register model for the arm64 UML backend.
 * arm64: X0..X30, SP, PC, PSTATE; syscall number in x8, args x0..x5,
 * result in x0.
 */
#ifndef __SYSDEP_ARM64_PTRACE_H
#define __SYSDEP_ARM64_PTRACE_H

#include <generated/user_constants.h>
#include <sysdep/faultinfo.h>

#define MAX_REG_OFFSET (UM_FRAME_SIZE)
#define MAX_REG_NR ((MAX_REG_OFFSET) / sizeof(unsigned long))

/*
 * Full gp-frame size in unsigned longs: the ABI frame plus the
 * UML-internal slots below (HOST_TLS, HOST_SYSCALLNO, HOST_ARG0).
 * Shared-core gp[] buffers size off this, never MAX_REG_NR + N.
 */
#define UM_GP_SLOTS (MAX_REG_NR + 3)

/* UML-internal slot past the ABI frame (x0..x30, sp, pc, pstate): the
 * guest's TPIDR_EL0 value. */
#define HOST_TLS 34

/* Second UML-internal slot: the syscall number, mirrored from x8 by the
 * regset marshal. PT_SYSCALL_NR must not alias a live GPR: process.c's
 * -ERESTARTSYS guard writes -1 into it after every relayed signal, and
 * x8 is a compiler-used register (x86's ORIG_RAX is dead storage). */
#define HOST_SYSCALLNO 35

/* Third UML-internal slot: the trap-time x0, mirrored by the regset
 * marshal / get_stub_state. handle_syscall writes the default return
 * (-ENOSYS) into x0 BEFORE reading args, harmless on x86 (rax is not
 * an argument register) but on arm64 x0 is both return and arg1, so
 * UPT_SYSCALL_ARG1 reads this snapshot instead. */
#define HOST_ARG0 36

#define REGS_Xn(r, n) ((r)[HOST_X0 + (n)])
#define REGS_PC(r) ((r)[HOST_PC])
#define REGS_SP(r) ((r)[HOST_SP])
#define REGS_PSTATE(r) ((r)[HOST_PSTATE])

#define UPT_IP(r) REGS_PC((r)->gp)
#define UPT_SP(r) REGS_SP((r)->gp)
#define UPT_PSTATE(r) REGS_PSTATE((r)->gp)

#define UPT_SYSCALL_ARG1(r) ((r)->gp[HOST_ARG0])

/*
 * When to seed the return register with -ENOSYS before the
 * syscall-entry trace stop (see handle_syscall()).
 *
 * x86 does it unconditionally, and that is faithful there: native x86
 * really does leave -ENOSYS in RAX at an entry stop, and RAX is not an
 * argument register. On arm64 the return value and the first argument
 * are both x0, so seeding early makes a guest tracer reading the
 * classic register view see 0xffffffffffffffda as argument 1 of every
 * syscall; only tracers using PTRACE_GET_SYSCALL_INFO escape, because
 * that routes through syscall_get_arguments() and the HOST_ARG0
 * snapshot. (The syscall dispatch itself always reads HOST_ARG0, so
 * the seed never corrupted arguments here; this is about what a guest
 * tracer observes.)
 *
 * Native arm64 seeds before the stop in exactly one case: when the
 * incoming number is already NO_SYSCALL, so that a tracer which skips
 * the call without setting x0 still gets an error rather than a stale
 * argument (arch/arm64/kernel/syscall.c). Match that.
 */
#define UM_SEED_ENOSYS_BEFORE_TRACE(r)	((long)UPT_SYSCALL_NR(r) < 0)
#define UPT_SYSCALL_ARG2(r) ((r)->gp[HOST_X1])
#define UPT_SYSCALL_ARG3(r) ((r)->gp[HOST_X2])
#define UPT_SYSCALL_ARG4(r) ((r)->gp[HOST_X3])
#define UPT_SYSCALL_ARG5(r) ((r)->gp[HOST_X4])
#define UPT_SYSCALL_ARG6(r) ((r)->gp[HOST_X5])

extern unsigned long host_fp_size;

struct uml_pt_regs {
	unsigned long gp[UM_GP_SLOTS]; /* ABI frame + HOST_TLS/SYSCALLNO/ARG0 */
	struct faultinfo faultinfo;
	long syscall;
	int is_user;

	/* Dynamically sized FP registers (fpsimd/SVE) */
	unsigned long fp[];
};

#define EMPTY_UML_PT_REGS { }

#define UPT_SYSCALL_NR(r) ((r)->syscall)
#define UPT_FAULTINFO(r) (&(r)->faultinfo)
#define UPT_IS_USER(r) ((r)->is_user)

extern int arch_init_registers(int pid);

/* Initial stack pointer for a new thread: arm64 hardware faults on
 * SP-relative access with SP not 16-byte aligned (SIGBUS). */
#define UM_THREAD_START_SP(stack) \
	(((unsigned long)(stack) + UM_THREAD_SIZE - sizeof(void *)) & ~15UL)

#endif /* __SYSDEP_ARM64_PTRACE_H */
