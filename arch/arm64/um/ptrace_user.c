// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <errno.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <ptrace_user.h>
#include <sysdep/ptrace.h>
#include <sysdep/ptrace_user.h>
#include <linux/elf.h>

int ptrace_getregs(long pid, unsigned long *regs_out)
{
	struct iovec iov = {
		.iov_base = regs_out,
		.iov_len = UM_FRAME_SIZE,
	};
	unsigned long tls = 0;
	struct iovec tls_iov = {
		.iov_base = &tls,
		.iov_len = sizeof(tls),
	};

	if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &iov) < 0)
		return -errno;

	/* Mirror the syscall number into the dead HOST_SYSCALLNO slot,
	 * and trap-time x0 into HOST_ARG0 (handle_syscall's default-return
	 * write clobbers live x0; the arm64 return register is arg1). */
	regs_out[HOST_SYSCALLNO] = regs_out[HOST_X8];
	regs_out[HOST_ARG0] = regs_out[HOST_X0];

	/*
	 * TPIDR_EL0 is not part of NT_PRSTATUS on arm64; it has its own
	 * regset and is ordinary per-thread state, available at every
	 * stop. Read it into the HOST_TLS slot so the saved frame really
	 * carries the thread's TLS: a forked child inherits its parent's
	 * gp[] including this slot, which is exactly the fork() semantic
	 * for TPIDR_EL0.
	 *
	 * The read is best-effort on purpose: on failure the slot keeps
	 * the value it already had, which is the thread's last saved TLS.
	 * NT_ARM_TLS is provided by every arm64 host kernel, so the
	 * failure arm is not expected to be taken.
	 */
	if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_ARM_TLS, &tls_iov) == 0)
		regs_out[HOST_TLS] = tls;

	return 0;
}

/*
 * Write the guest's registers back to the stub process.
 *
 * NT_ARM_SYSTEM_CALL is deliberately NOT written here. On x86 the
 * syscall number is orig_ax, an ordinary member of the register set,
 * so restoring registers restores it too. On arm64 it is a separate
 * regset that does not describe thread state at all: it selects which
 * syscall the *currently stopped* syscall entry will execute. Writing
 * it during a general register restore does not reinstate a thread's
 * number, it redirects whatever syscall the stub happens to be sitting
 * in -- harmless while one guest thread owns a stub process, actively
 * wrong as soon as two do, since all threads of a guest mm share one
 * stub. It is written only where UML deliberately changes the syscall
 * in flight (sysdep_ptrace_pokeuser with PT_SYSCALL_NR_OFFSET).
 *
 * NT_ARM_TLS, by contrast, IS per-thread state and must be pushed on
 * every restore, or switching guest threads inside one stub process
 * would leave the previous thread's TPIDR_EL0 in place.
 */
int ptrace_setregs(long pid, unsigned long *regs)
{
	struct iovec iov = {
		.iov_base = regs,
		.iov_len = UM_FRAME_SIZE,
	};
	struct iovec tls_iov = {
		.iov_base = &regs[HOST_TLS],
		.iov_len = sizeof(unsigned long),
	};

	if (ptrace(PTRACE_SETREGSET, pid, (void *)NT_PRSTATUS, &iov) < 0)
		return -errno;

	if (ptrace(PTRACE_SETREGSET, pid, (void *)NT_ARM_TLS, &tls_iov) < 0)
		return -errno;

	return 0;
}

const char *ptrace_reg_name(int idx)
{
#define R(n) case HOST_##n: return #n

	switch (idx) {
	R(X0); R(X1); R(X2); R(X3); R(X4); R(X5); R(X6); R(X7);
	R(X8); R(X9); R(X10); R(X11); R(X12); R(X13); R(X14);
	R(X15); R(X16); R(X17); R(X18); R(X19); R(X20); R(X21);
	R(X22); R(X23); R(X24); R(X25); R(X26); R(X27); R(X28);
	R(X29); R(X30);
	R(SP); R(PC); R(PSTATE);
	}
	return "";
}

long sysdep_ptrace_peekuser(long pid, long off, long *val)
{
	unsigned long regs[UM_GP_SLOTS]; /* full gp frame incl. internal slots */
	int err;

	if (off == PT_SYSCALL_NR_OFFSET) {
		/* syscallno is its own regset on arm64 (x8 writes are not
		 * reloaded; verified empirically on 6.8) */
		int nr;
		struct iovec iov = { .iov_base = &nr, .iov_len = sizeof(nr) };

		if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_ARM_SYSTEM_CALL,
			   &iov) < 0)
			return -errno;
		*val = nr;
		return 0;
	}

	if (off < 0 || off >= UM_FRAME_SIZE)
		return -EIO;
	err = ptrace_getregs(pid, regs);
	if (err)
		return err;
	*val = regs[off / sizeof(long)];
	return 0;
}

long sysdep_ptrace_pokeuser(long pid, long off, long val)
{
	unsigned long regs[UM_GP_SLOTS]; /* full gp frame incl. internal slots */
	int err;

	if (off == PT_SYSCALL_NR_OFFSET) {
		int nr = val;
		struct iovec iov = { .iov_base = &nr, .iov_len = sizeof(nr) };

		if (ptrace(PTRACE_SETREGSET, pid, (void *)NT_ARM_SYSTEM_CALL,
			   &iov) < 0)
			return -errno;
		return 0;
	}

	if (off < 0 || off >= UM_FRAME_SIZE)
		return -EIO;
	err = ptrace_getregs(pid, regs);
	if (err)
		return err;
	regs[off / sizeof(long)] = val;
	return ptrace_setregs(pid, regs);
}
