// SPDX-License-Identifier: GPL-2.0
/*
 * show_regs for the arm64 UML backend (x0..x30, sp, pc, pstate),
 * mirroring arch/x86/um/sysrq_64.c's structure.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/utsname.h>
#include <asm/current.h>
#include <asm/ptrace.h>

void show_regs(struct pt_regs *regs)
{
	int i;

	printk("\n");
	print_modules();
	printk(KERN_INFO "Pid: %d, comm: %.20s %s %s\n", task_pid_nr(current),
		current->comm, print_tainted(), init_utsname()->release);
	printk(KERN_INFO "pc : %016lx  pstate : %08lx\n",
	       (unsigned long)PT_REGS_IP(regs),
	       (unsigned long)PT_REGS_PSTATE(regs));
	printk(KERN_INFO "sp : %016lx\n", PT_REGS_SP(regs));
	for (i = 0; i < 31; i += 2) {
		if (i + 1 < 31)
			printk(KERN_INFO "x%-2d: %016lx x%-2d: %016lx\n",
			       i, regs->regs.gp[i], i + 1, regs->regs.gp[i + 1]);
		else
			printk(KERN_INFO "x%-2d: %016lx\n",
			       i, regs->regs.gp[i]);
	}
}
