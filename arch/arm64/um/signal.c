// SPDX-License-Identifier: GPL-2.0
/*
 * Signal frame construction for the arm64 UML backend, a stub for
 * bring-up: delivery fails with -ENOSYS until the rt_sigframe
 * implementation lands (the next surface; sys_rt_sigreturn is already
 * ni-stubbed in the table with the same note).
 */
#include <linux/errno.h>
#include <linux/signal.h>
#include <sysdep/ptrace.h>

int setup_signal_stack_si(unsigned long stack_top, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *mask)
{
	return -ENOSYS;
}
