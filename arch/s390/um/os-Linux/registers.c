// SPDX-License-Identifier: GPL-2.0
/*
 * Host register access for the s390x UML backend: FP register IO via
 * the NT_PRFPREG regset (fixed 136-byte s390_fp_regs frame — no
 * probing, no SVE-style discovery; vector state NT_S390_VXRS_* is
 * skipped in v0 like arm64 skipped SVE), and thread-register reads
 * out of UML's own jmp_buf for wchan/stack walking.
 */
#include <errno.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <longjmp.h>
#include <sysdep/ptrace_user.h>
#include <sys/uio.h>
#include <linux/elf.h>
#include <registers.h>

/* v0: fixed fp frame; NT_S390_VXRS_* comes later if ever needed */
unsigned long host_fp_size = UM_S390_FP_SIZE;

int get_fp_registers(int pid, unsigned long *regs)
{
	struct iovec iov = {
		.iov_base = regs,
		.iov_len = host_fp_size,
	};

	if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRFPREG, &iov) < 0)
		return -errno;
	return 0;
}

int put_fp_registers(int pid, unsigned long *regs)
{
	struct iovec iov = {
		.iov_base = regs,
		.iov_len = host_fp_size,
	};

	if (ptrace(PTRACE_SETREGSET, pid, (void *)NT_PRFPREG, &iov) < 0)
		return -errno;
	return 0;
}

unsigned long get_thread_reg(int reg, jmp_buf *buf)
{
	switch (reg) {
	case HOST_PSW_ADDR:
		return buf[0]->__r14;		/* IP lives in r14 slot */
	case HOST_SP:
		return buf[0]->__r15;
	default:
		return 0;
	}
}


int arch_init_registers(int pid)
{
	/*
	 * Nothing to probe: NT_PRFPREG is a fixed-size frame on s390.
	 */
	return 0;
}
