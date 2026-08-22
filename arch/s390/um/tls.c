// SPDX-License-Identifier: GPL-2.0
/*
 * TLS handling for the s390x UML backend — the pointer lives in
 * acrs[0..1] (big-endian hi/lo), part of the ptrace register frame,
 * so context switches and exec need no arch work; set it on
 * CLONE_SETTLS. Native s390 does exactly this split in
 * copy_thread: acrs[0]=tls>>32, acrs[1]=(u32)tls.
 */
#include <linux/sched.h>
#include <asm/ptrace.h>

/*
 * HOST_ACRS holds acrs[0..1] packed in one long: high half is
 * acrs[0] (= tls >> 32), low half is acrs[1] (= (u32)tls) — big-
 * endian split by ABI definition. Same packing the mcontext marshal
 * uses, so the value flows to the stub frame unchanged.
 */

void arch_switch_to(struct task_struct *to)
{
	/*
	 * Nothing needs to be done on s390: the TLS pointer is saved in
	 * the ptrace register frame and restored through the stub's
	 * mcontext round-trip.
	 */
}

void clear_flushed_tls(struct task_struct *task)
{
}

int arch_set_tls(struct task_struct *t, unsigned long tls)
{
	unsigned long *gp = t->thread.regs.regs.gp;

	gp[HOST_ACRS] = ((unsigned long)(unsigned int)(tls >> 32)) << 32 |
			(unsigned int)tls;

	return 0;
}
