// SPDX-License-Identifier: GPL-2.0
/*
 * Signal frame construction and restoration for the arm64 UML backend:
 * the native arm64 rt_sigframe (siginfo + ucontext with the 4384-byte
 * mcontext including the fpsimd extra record) on the guest's stack.
 * arm64 has no SA_RESTORER: the handler's return address (x30) is set
 * to the guest vDSO's __kernel_rt_sigreturn trampoline.
 */
#include <linux/ptrace.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <asm/unistd.h>
#include <frame_kern.h>
#include <registers.h>
#include <skas.h>
#include <sysdep/ptrace.h>
#include <sysdep/ptrace_user.h>
#include <asm/sigcontext.h>

extern unsigned long um_vdso_sigreturn;

/* arm64 sigcontext: the mcontext part of the ABI ucontext */
struct um_sigcontext {
	__u64 fault_address;
	__u64 regs[31];
	__u64 sp;
	__u64 pc;
	__u64 pstate;
	__u8 __reserved[4096] __aligned(16);
};

/* ABI layout: siginfo + ucontext{flags, link, stack, sigmask(1024b), mcontext} */
struct um_rt_sigframe {
	siginfo_t info;
	unsigned long uc_flags;
	void *uc_link;
	stack_t uc_stack;
	sigset_t uc_sigmask;
	__u8 __unused[1024 / 8 - sizeof(sigset_t)];
	struct um_sigcontext sc;
};

static void build_sc(struct um_sigcontext *sc, struct pt_regs *regs,
		     struct ksignal *ksig)
{
	struct uml_pt_regs *ur = &regs->regs;
	struct um_fpsimd_state *fp = (void *)ur->fp;
	struct um_fpsimd_record *rec = (void *)sc->__reserved;
	struct {
		__u32 magic;
		__u32 size;
	} *end;
	int i;

	/*
	 * Native arm64 fills fault_address with the faulting address for
	 * fault signals; mirror that from the siginfo, which carries
	 * si_addr exactly for the fault-layout signals.
	 */
	if (siginfo_layout(ksig->sig, ksig->info.si_code) == SIL_FAULT)
		sc->fault_address = (unsigned long)ksig->info.si_addr;
	else
		sc->fault_address = 0;
	for (i = 0; i < 31; i++)
		sc->regs[i] = ur->gp[i];
	sc->sp = ur->gp[HOST_SP];
	sc->pc = ur->gp[HOST_PC];
	sc->pstate = ur->gp[HOST_PSTATE];

	/*
	 * The fpsimd extra record, then the terminator. The record is
	 * laid out as the ABI's struct fpsimd_context {magic, size, fpsr,
	 * fpcr, vregs}, 528 bytes, converted from the canonical
	 * user_fpsimd_state layout of regs->fp.
	 */
	BUILD_BUG_ON(sizeof(struct user_fpsimd_struct) !=
		     UM_FPSIMD_STATE_SIZE);
	BUILD_BUG_ON(sizeof(struct um_fpsimd_record) !=
		     sizeof(struct fpsimd_context));
	BUILD_BUG_ON(offsetof(struct um_fpsimd_record, fpsr) !=
		     offsetof(struct fpsimd_context, fpsr));
	BUILD_BUG_ON(offsetof(struct um_fpsimd_record, fpcr) !=
		     offsetof(struct fpsimd_context, fpcr));
	BUILD_BUG_ON(offsetof(struct um_fpsimd_record, vregs) !=
		     offsetof(struct fpsimd_context, vregs));
	rec->magic = FPSIMD_MAGIC;
	rec->size = UM_FPSIMD_RECORD_SIZE;
	rec->fpsr = fp->fpsr;
	rec->fpcr = fp->fpcr;
	memcpy(rec->vregs, fp->vregs, sizeof(rec->vregs));
	end = (void *)(rec + 1);
	end->magic = 0;
	end->size = 0;
}

int setup_signal_stack_si(unsigned long stack_top, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *mask)
{
	struct um_rt_sigframe __user *frame;
	struct um_rt_sigframe *kf;
	struct uml_pt_regs *ur = &regs->regs;
	int err, sig = ksig->sig;

	stack_top = round_down(stack_top, 16);
	frame = (struct um_rt_sigframe __user *)stack_top - 1;
	if (!access_ok(frame, sizeof(*frame)))
		return 1;

	kf = kzalloc(sizeof(*kf), GFP_KERNEL);
	if (!kf)
		return 1;

	/* uc_flags/uc_link stay zero; the ABI mask area is 1024 bits */
	memcpy(&kf->uc_sigmask, mask, sizeof(*mask));
	build_sc(&kf->sc, regs, ksig);

	err = copy_to_user(frame, kf, sizeof(*kf));
	kfree(kf);
	if (err)
		return 1;

	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		if (copy_siginfo_to_user(&frame->info, &ksig->info))
			return 1;
	}
	if (__save_altstack(&frame->uc_stack, PT_REGS_SP(regs)))
		return 1;

	PT_REGS_SP(regs) = (unsigned long)frame;
	PT_REGS_IP(regs) = (unsigned long)ksig->ka.sa.sa_handler;
	ur->gp[HOST_X0] = sig;
	ur->gp[HOST_X1] = (unsigned long)&frame->info;
	ur->gp[HOST_X2] = (unsigned long)&frame->uc_flags;
	/* arm64 has no SA_RESTORER: x30 points at the guest vDSO */
	ur->gp[HOST_X30] = um_vdso_sigreturn;
	return 0;
}

static int restore_sc(struct pt_regs *regs, struct um_sigcontext __user *from)
{
	struct uml_pt_regs *ur = &regs->regs;
	struct {
		__u64 fault_address;
		__u64 regs[31];
		__u64 sp, pc, pstate;
	} fixed;
	struct {
		__u32 magic;
		__u32 size;
	} hdr;
	int i;

	if (copy_from_user(&fixed, from, sizeof(fixed)))
		return 1;

	for (i = 0; i < 31; i++)
		ur->gp[i] = fixed.regs[i];
	ur->gp[HOST_SP] = fixed.sp;
	ur->gp[HOST_PC] = fixed.pc;
	ur->gp[HOST_PSTATE] = fixed.pstate;

	if (copy_from_user(&hdr, (void __user *)from +
			 offsetof(struct um_sigcontext, __reserved), sizeof(hdr)))
		return 1;
	if (hdr.magic == FPSIMD_MAGIC) {
		struct um_fpsimd_state *fp = (void *)ur->fp;
		struct um_fpsimd_record rec;

		/*
		 * Exact ABI size, as native restore_fpsimd_context()
		 * checks: sizeof(struct fpsimd_context), 528.
		 */
		if (hdr.size != UM_FPSIMD_RECORD_SIZE)
			return 1;
		if (copy_from_user(&rec, (void __user *)from +
				   offsetof(struct um_sigcontext, __reserved),
				   sizeof(rec)))
			return 1;

		/* record order {fpsr, fpcr, vregs} -> canonical regs->fp */
		memcpy(fp->vregs, rec.vregs, sizeof(fp->vregs));
		fp->fpsr = rec.fpsr;
		fp->fpcr = rec.fpcr;
		fp->__reserved[0] = 0;
		fp->__reserved[1] = 0;
	}

	return 0;
}

SYSCALL_DEFINE0(rt_sigreturn)
{
	unsigned long sp = PT_REGS_SP(&current->thread.regs);
	struct um_rt_sigframe __user *frame =
		(struct um_rt_sigframe __user *)sp;
	sigset_t set;

	if (copy_from_user(&set, &frame->uc_sigmask, sizeof(set)))
		goto segfault;
	set_current_blocked(&set);

	if (restore_sc(&current->thread.regs, &frame->sc))
		goto segfault;

	/* Avoid ERESTART handling */
	PT_REGS_SYSCALL_NR(&current->thread.regs) = -1;
	return PT_REGS_SYSCALL_RET(&current->thread.regs);

segfault:
	force_sig(SIGSEGV);
	return 0;
}
