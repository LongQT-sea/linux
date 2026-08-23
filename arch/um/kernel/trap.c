// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/sched/debug.h>
#include <linux/uprobes.h>
#include <linux/kprobes.h>
#include <asm/current.h>
#include <asm/tlbflush.h>
#include <arch.h>
#include <as-layout.h>
#include <kern_util.h>
#include <os.h>
#include <skas.h>

static void show_segv_info(struct uml_pt_regs *regs)
{
	struct task_struct *tsk = current;
	struct faultinfo *fi = UPT_FAULTINFO(regs);

	if (!unhandled_signal(tsk, SIGSEGV))
		return;

	if (!printk_ratelimit())
		return;

	printk("%s%s[%d]: segfault at %lx ip %px sp %px error %x",
		task_pid_nr(tsk) > 1 ? KERN_INFO : KERN_EMERG,
		tsk->comm, task_pid_nr(tsk), FAULT_ADDRESS(*fi),
		(void *)UPT_IP(regs), (void *)UPT_SP(regs),
		fi->error_code);

	print_vma_addr(KERN_CONT " in ", UPT_IP(regs));
	printk(KERN_CONT "\n");
}

static void bad_segv(struct faultinfo fi, unsigned long ip)
{
	current->thread.arch.faultinfo = fi;
	force_sig_fault(SIGSEGV, SEGV_ACCERR, (void __user *) FAULT_ADDRESS(fi));
}

void fatal_sigsegv(void)
{
	force_fatal_sig(SIGSEGV);
	do_signal(&current->thread.regs);
	/*
	 * This is to tell gcc that we're not returning - do_signal
	 * can, in general, return, but in this case, it's not, since
	 * we just got a fatal SIGSEGV queued.
	 */
	os_dump_core();
}

#ifdef CONFIG_KPROBES
/* async signals masked in the signal frame for a pending single-step */
static unsigned long kprobe_ss_masked;

/*
 * On native x86, setup_singlestep() clears EFLAGS.IF in the saved regs,
 * so no interrupt can arrive while a kprobe's displaced instruction runs
 * from its out-of-line slot. Under UML, interrupts are host signals and
 * the saved EFLAGS have no effect on their delivery; an interrupt
 * handler running inside the single-step window could hit another kprobe
 * and corrupt the per-cpu kprobe state (a class of reentry the native
 * IF-clear rules out). Mask the interrupt signals in the signal frame
 * while a step is pending, and unmask them once no step is pending
 * anymore (the completing int3, or the fault path in segv()).
 */
static void kprobe_update_ss_mask(void *mc)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (kprobe_running() &&
	    (kcb->kprobe_status == KPROBE_HIT_SS ||
	     kcb->kprobe_status == KPROBE_REENTER)) {
		kprobe_ss_masked |= mc_mask_async_signals(mc);
	} else if (kprobe_ss_masked) {
		mc_unmask_async_signals(mc, kprobe_ss_masked);
		kprobe_ss_masked = 0;
	}
}
NOKPROBE_SYMBOL(kprobe_update_ss_mask);
#endif

/**
 * segv_handler() - the SIGSEGV handler
 * @sig:	the signal number
 * @unused_si:	the signal info struct; unused in this handler
 * @regs:	the ptrace register information
 * @mc:		the mcontext of the signal
 *
 * The handler first extracts the faultinfo from the UML ptrace regs struct.
 * If the userfault did not happen in an UML userspace process, bad_segv is called.
 * Otherwise the signal did happen in a cloned userspace process, handle it.
 */
void segv_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs,
		  void *mc)
{
	struct faultinfo * fi = UPT_FAULTINFO(regs);

	if (UPT_IS_USER(regs) && !SEGV_IS_FIXABLE(fi)) {
		show_segv_info(regs);
		bad_segv(*fi, UPT_IP(regs));
		return;
	}
	segv(*fi, UPT_IP(regs), UPT_IS_USER(regs), regs, mc);
}
NOKPROBE_SYMBOL(segv_handler);

/*
 * We give a *copy* of the faultinfo in the regs to segv.
 * This must be done, since nesting SEGVs could overwrite
 * the info in the regs. A pointer to the info then would
 * give us bad data!
 */
unsigned long segv(struct faultinfo fi, unsigned long ip, int is_user,
		   struct uml_pt_regs *regs, void *mc)
{
	int si_code;
	int err;
	int is_write = FAULT_WRITE(fi);
	unsigned long address = FAULT_ADDRESS(fi);

	if (!is_user && regs)
		current->thread.segv_regs = container_of(regs, struct pt_regs, regs);

#ifdef CONFIG_KPROBES
	/*
	 * Mirror the kprobe_page_fault() hook at the top of the native x86
	 * page-fault handler: on a fault while a kprobe's displaced
	 * instruction runs from its out-of-line slot,
	 * kprobe_fault_handler() rewinds the saved ip to the probed
	 * address and unwinds the kprobe state, and normal fault handling
	 * continues from there. The host restores register state from the
	 * mcontext on sigreturn, so a rewound ip must be written back, and
	 * interrupt signals masked for the single-step window must be
	 * unmasked again (see relay_signal()). The trap number argument is
	 * only inspected by arch fault handlers that care; um's does not.
	 */
	if (!is_user && regs && mc) {
		struct pt_regs *pregs = container_of(regs, struct pt_regs,
						     regs);
		unsigned long saved_ip = UPT_IP(regs);
		bool handled = kprobe_page_fault(pregs, 0);

		if (handled || UPT_IP(regs) != saved_ip) {
			kprobe_update_ss_mask(mc);
			mc_set_regs(regs, mc, 0);
		}
		if (handled)
			goto out;
	}
#endif

	if (!is_user && address >= start_vm && address < end_vm) {
		/*
		 * Kernel has pending updates from set_ptes that were not
		 * flushed yet. Syncing them should fix the pagefault (if not
		 * we'll get here again and panic).
		 */
		err = um_tlb_sync(&init_mm);
		if (err == -ENOMEM)
			report_enomem();
		if (err)
			panic("Failed to sync kernel TLBs: %d", err);
		goto out;
	}
	else if (current->pagefault_disabled) {
		if (!mc) {
			show_regs(container_of(regs, struct pt_regs, regs));
			panic("Segfault with pagefaults disabled but no mcontext: addr 0x%lx, ip 0x%lx, %s",
			      address, ip, is_write ? "write" : "read");
		}
		if (!current->thread.segv_continue) {
			show_regs(container_of(regs, struct pt_regs, regs));
			panic("Segfault without recovery target: addr 0x%lx, ip 0x%lx, %s",
			      address, ip, is_write ? "write" : "read");
		}
		mc_set_rip(mc, current->thread.segv_continue);
		current->thread.segv_continue = NULL;
		goto out;
	}
	else if (current->mm == NULL) {
		show_regs(container_of(regs, struct pt_regs, regs));
		panic("Segfault with no mm: addr 0x%lx, ip 0x%lx, %s",
			      address, ip, is_write ? "write" : "read");
	}
	else if (!is_user && address > PAGE_SIZE && address < TASK_SIZE) {
		show_regs(container_of(regs, struct pt_regs, regs));
		panic("Kernel tried to access user memory at addr 0x%lx, ip 0x%lx",
		       address, ip);
	}

	if (SEGV_IS_FIXABLE(&fi))
		err = handle_page_fault(address, ip, is_write, is_user,
					&si_code);
	else {
		err = -EFAULT;
		/*
		 * A thread accessed NULL, we get a fault, but CR2 is invalid.
		 * This code is used in __do_copy_from_user() of TT mode.
		 * XXX tt mode is gone, so maybe this isn't needed any more
		 */
		address = 0;
	}

	if (!err)
		goto out;
	else if (!is_user && arch_fixup(ip, regs))
		goto out;

	if (!is_user) {
		show_regs(container_of(regs, struct pt_regs, regs));
		panic("Kernel mode fault at addr 0x%lx, ip 0x%lx",
		      address, ip);
	}

	show_segv_info(regs);

	if (err == -EACCES) {
		current->thread.arch.faultinfo = fi;
		force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address);
	} else {
		BUG_ON(err != -EFAULT);
		current->thread.arch.faultinfo = fi;
		force_sig_fault(SIGSEGV, si_code, (void __user *) address);
	}

out:
	if (regs)
		current->thread.segv_regs = NULL;

	return 0;
}
NOKPROBE_SYMBOL(segv);

void relay_signal(int sig, struct siginfo *si, struct uml_pt_regs *regs,
		  void *mc)
{
	int code, err;
	if (!UPT_IS_USER(regs)) {
#ifdef CONFIG_KPROBES
		/*
		 * A kernel-mode int3 (kprobe hit, or completion of a
		 * displaced-instruction step from an out-of-line slot)
		 * arrives here as a host SIGTRAP. UML raises no die chain,
		 * so join the kprobe core directly (the arm64/riscv
		 * pattern, same as the user-mode uprobe relay below).
		 * sig_handler_common() only fills the regs shell from the
		 * mcontext for SIGSEGV, so do it here; on a consumed trap,
		 * write the (possibly redirected) state back, since the
		 * mcontext is what the host restores on sigreturn.
		 */
		if (sig == SIGTRAP && mc) {
			struct pt_regs *pregs =
				container_of(regs, struct pt_regs, regs);

			mc_get_regs(regs, mc);
			if (kprobe_int3_handler(pregs)) {
				kprobe_update_ss_mask(mc);
				mc_set_regs(regs, mc, 0);
				return;
			}
		}
#endif
		if (sig == SIGBUS)
			printk(KERN_ERR "Bus error - the host /dev/shm or /tmp "
			       "mount likely just ran out of space\n");
		panic("Kernel mode signal %d", sig);
	}

	/*
	 * Uprobe traps: a guest-user int3 arrives as SIGTRAP, and so does the
	 * completion of an out-of-line single-step. si_code tells them apart:
	 * TRAP_TRACE is always the single-step; a breakpoint hit is
	 * TRAP_BRKPT in seccomp mode, but the host reports int3 stops with
	 * SI_KERNEL under PTRACE_SYSEMU. Feed them to the uprobe core
	 * directly (the arm64/riscv pattern); if consumed, the guest task
	 * must not see a signal.
	 */
#ifdef CONFIG_UPROBES
	if (sig == SIGTRAP && si) {
		struct pt_regs *pregs = container_of(regs, struct pt_regs, regs);

		if (si->si_code == TRAP_TRACE) {
			if (uprobe_post_sstep_notifier(pregs))
				return;
		} else if (si->si_code == TRAP_BRKPT || si->si_code == SI_KERNEL) {
			if (uprobe_pre_sstep_notifier(pregs))
				return;
		}
	}
#endif

	arch_examine_signal(sig, regs);

	/* Is the signal layout for the signal known?
	 * Signal data must be scrubbed to prevent information leaks.
	 */
	code = si->si_code;
	err = si->si_errno;
	if ((err == 0) && (siginfo_layout(sig, code) == SIL_FAULT)) {
		struct faultinfo *fi = UPT_FAULTINFO(regs);
		current->thread.arch.faultinfo = *fi;
		force_sig_fault(sig, code, (void __user *)FAULT_ADDRESS(*fi));
	} else {
		printk(KERN_ERR "Attempted to relay unknown signal %d (si_code = %d) with errno %d\n",
		       sig, code, err);
		force_sig(sig);
	}
}
NOKPROBE_SYMBOL(relay_signal);

void winch(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs,
	   void *mc)
{
	do_IRQ(WINCH_IRQ, regs);
}
