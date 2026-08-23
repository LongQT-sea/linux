// SPDX-License-Identifier: GPL-2.0
/*
 * Host register access for the arm64 UML backend: FP register IO via
 * the NT_PRFPREG regset (the fixed 528-byte user_fpsimd_state frame,
 * which is also the canonical regs->fp layout; SVE discovery is a
 * later surface), and thread-register reads out of UML's own jmp_buf
 * for wchan/stack walking.
 */
#include <errno.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <longjmp.h>
#include <sysdep/ptrace_user.h>
#include <sys/uio.h>
#include <linux/elf.h>
#include <registers.h>
#include <sys/mman.h>

/* Fixed fpsimd frame; SVE length discovery comes later */
unsigned long host_fp_size = UM_FPSIMD_STATE_SIZE;

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
	case HOST_SP:
		return buf[0]->__sp;
	case HOST_PC:
		return buf[0]->__pc;
	case HOST_X29:
		return buf[0]->__fp;
	default:
		return 0;
	}
}

int arch_init_registers(int pid)
{
	/*
	 * NT_PRFPREG is a fixed-size frame on arm64 (unlike x86 xstate):
	 * nothing to probe in v0. SVE vector-length discovery comes with
	 * the SVE surface later.
	 */
	return 0;
}
