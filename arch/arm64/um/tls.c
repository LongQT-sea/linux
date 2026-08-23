// SPDX-License-Identifier: GPL-2.0
/*
 * TLS (TPIDR_EL0) handling for the arm64 UML backend: the value
 * lives in the ptrace register set (HOST_TLS slot), so context
 * switches and exec need no arch work; set it on CLONE_SETTLS.
 */
#include <linux/sched.h>
#include <asm/ptrace.h>

void arch_switch_to(struct task_struct *to)
{
	/*
	 * Nothing needs to be done on arm64.
	 * The TPIDR_EL0 value is saved in the ptrace register set and
	 * restored through the stub's arch_data channel.
	 */
}

void clear_flushed_tls(struct task_struct *task)
{
}

int arch_set_tls(struct task_struct *t, unsigned long tls)
{
	/*
	 * If CLONE_SETTLS is set, we need to save the thread id
	 * so it can be set during context switches.
	 */
	t->thread.regs.regs.gp[HOST_TLS] = tls;

	return 0;
}
