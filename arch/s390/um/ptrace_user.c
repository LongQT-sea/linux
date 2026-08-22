/*
 * Copyright (C) 2026 ox-alpha (s390x UML backend)
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel.com})
 *
 * Licensed under the GPL
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <ptrace_user.h>
#include <sysdep/ptrace.h>
#include <sysdep/ptrace_user.h>
#include <linux/elf.h>

#ifndef NT_S390_SYSTEM_CALL
#define NT_S390_SYSTEM_CALL 0x307
#endif

int ptrace_getregs(long pid, unsigned long *regs_out)
{
	struct iovec iov = {
		.iov_base = regs_out,
		.iov_len = UM_FRAME_SIZE,
	};

	if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &iov) < 0)
		return -errno;

	/*
	 * Mirror the live syscall number into the dead HOST_SYSCALLNO
	 * slot. Source order matters (probe-proven on 6.8.0-124):
	 * NT_S390_SYSTEM_CALL reads thread->system_call, which the
	 * kernel only refreshes at signal delivery or a tracer's own
	 * poke — NOT at every syscall entry, so it is stale garbage
	 * during check_ptrace's first intercept. The live nr at any
	 * syscall stop sits in gprs[1] (& 0xffff; __do_syscall itself
	 * falls back to gprs[1] when int_code's svc field is empty,
	 * arch/s390/kernel/syscall.c:113-118). Regset stays the poke
	 * channel: writing it sets int_code for real.
	 */
	regs_out[HOST_SYSCALLNO] = regs_out[HOST_GPR0 + 1] & 0xffff;
	regs_out[HOST_ARG0] = regs_out[HOST_ORIG_GPR2];
	regs_out[HOST_TLS] =
		(((unsigned long)(unsigned int)(regs_out[HOST_ACRS] >> 32)) << 32) |
		(unsigned int)(regs_out[HOST_ACRS]);
	return 0;
}

int ptrace_setregs(long pid, unsigned long *regs)
{
	struct iovec iov = {
		.iov_base = regs,
		.iov_len = UM_FRAME_SIZE,
	};


	if (ptrace(PTRACE_SETREGSET, pid, (void *)NT_PRSTATUS, &iov) < 0)
		return -errno;
	return 0;
}

const char *ptrace_reg_name(int idx)
{
#define R(n) case HOST_##n: return #n

	switch (idx) {
	R(PSW_MASK); R(PSW_ADDR);
	R(GPR0);
	case HOST_GPR0 + 1: return "GPR1";
	case HOST_GPR0 + 2: return "GPR2";
	case HOST_GPR0 + 3: return "GPR3";
	case HOST_GPR0 + 4: return "GPR4";
	case HOST_GPR0 + 5: return "GPR5";
	case HOST_GPR0 + 6: return "GPR6";
	case HOST_GPR0 + 7: return "GPR7";
	case HOST_GPR0 + 8: return "GPR8";
	case HOST_GPR0 + 9: return "GPR9";
	case HOST_GPR0 + 10: return "GPR10";
	case HOST_GPR0 + 11: return "GPR11";
	case HOST_GPR0 + 12: return "GPR12";
	case HOST_GPR0 + 13: return "GPR13";
	case HOST_GPR0 + 14: return "GPR14";
	R(SP);
	R(ORIG_GPR2);
	R(SYSCALLNO); R(ARG0); R(TLS);
	}
	return "";
}

long sysdep_ptrace_peekuser(long pid, long off, long *val)
{
	unsigned long regs[UM_GP_SLOTS]; /* full gp frame incl. internal slots */
	int err;

	if (off == PT_SYSCALL_NR_OFFSET) {
		/* Live nr at a syscall stop is gprs[1] (& 0xffff) —
		 * NT_S390_SYSTEM_CALL only reflects signal-delivery/
		 * poke-time int_code and reads stale otherwise (probe
		 * F-s6). Reads must not alias a live GPR, hence the
		 * dedicated path rather than the generic one below. */
		unsigned long regs[27]; /* psw.mask/addr + gprs + acrs + orig */
		struct iovec iov = { .iov_base = regs, .iov_len = sizeof(regs) };

		if (ptrace(PTRACE_GETREGSET, pid,
			   (void *)NT_PRSTATUS, &iov) < 0)
			return -errno;
		*val = regs[3] & 0xffff; /* gprs[1]: s390_regs layout */
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
		/* x86-style intercept: the parent rewrites the RESULT
		 * slot, not the nr. On s390 the syscall return value is
		 * gprs[2] (user offset slot 3 in NT_PRSTATUS), and a
		 * pre-set gprs[2] survives the svc (probe F-s6: kernel
		 * only overwrites it when the syscall actually runs its
		 * result path — with the nr left untouched, getpid's
		 * own write races nothing; empirically the preset wins
		 * because __do_syscall copies nr INTO gprs[2] at entry,
		 * but the entry-stop we are parked in is AFTER that
		 * copy — our poke lands last). Redirecting via
		 * int_code regset does NOT work on 6.8: thread->
		 * system_call is consumed only by signal/restart paths.
		 */
		unsigned long regs[27];
		struct iovec iov = { .iov_base = regs, .iov_len = sizeof(regs) };

		if (ptrace(PTRACE_GETREGSET, pid,
			   (void *)NT_PRSTATUS, &iov) < 0)
			return -errno;
		regs[4] = (unsigned long)val; /* gprs[2]: s390_regs layout */
		if (ptrace(PTRACE_SETREGSET, pid,
			   (void *)NT_PRSTATUS, &iov) < 0)
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
