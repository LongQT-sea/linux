// SPDX-License-Identifier: GPL-2.0
/*
 * Signal frame construction and restoration for the s390x UML backend:
 * the native s390 rt_sigframe (svc trampoline word + siginfo +
 * ucontext whose uc_mcontext is the flat _sigregs) on the guest's
 * stack. s390 HAS SA_RESTORER — the handler returns through the
 * restorer in r14, which points at the guest vDSO's sigreturn
 * trampoline (x86 model, not arm64's no-restorer dance).
 */
#include <linux/ptrace.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <asm/unistd.h>
#include <sysdep/ptrace_user.h>
#include <as-layout.h>
#include <frame_kern.h>
#include <registers.h>
#include <skas.h>
#include <sysdep/ptrace.h>
/*
 * PSW user-legal bits (uapi/asm/ptrace.h values) — defined locally
 * because that header's struct user_regs_struct collides with the
 * backend-owned one in asm/ptrace.h.
 */
#define UM_S390_PSW_MASK_USER	_AC(0x0000FF0180000000, UL)
#define UM_S390_PSW_MASK_PSTATE	_AC(0x0001000000000000, UL)

extern unsigned long um_vdso_sigreturn;

/*
 * Frame trampoline: `svc __NR_rt_sigreturn` — the SVC instruction is
 * opcode 0x0a with the immediate number in the low halfword byte
 * (big-endian halfword 0x0aNN). __NR_rt_sigreturn = 173 on s390x.
 */
#define S390_SYS_SIGRETURN	((__u16)(0x0a00 | 173))

/*
 * ABI layout mirrors arch/s390/kernel/signal.c's rt_sigframe:
 *   160-byte callee area, svc insn, siginfo, ucontext {flags, link,
 *   stack(24), mcontext(_sigregs: psw+gprs+acrs+fpregs), sigmask}.
 * No _sigregs_ext in v0: vector state is skipped like arm64-no-SVE.
 */

/*
 * We construct the frame directly in glibc's layout so that a guest
 * libc unwinds it natively. Rather than redefining kernel types,
 * embed the exact byte image via the same field order as
 * struct rt_sigframe in arch/s390/kernel/signal.c:
 */
struct um_rt_sigframe {
	unsigned char callee_used_stack[160]; /* uapi __SIGNAL_FRAMESIZE */
	unsigned short svc_insn;
	siginfo_t info;
	unsigned long uc_flags;
	struct um_rt_sigframe *uc_link;
	stack_t uc_stack;
	struct um_mcontext {
		struct {
			unsigned long mask;
			unsigned long addr;
		} __attribute__((aligned(8))) psw;
		unsigned long gprs[16];
		unsigned int acrs[16];
		struct {
			unsigned int fpc;
			unsigned int pad;
			double fprs[16];
		} fpregs;
	} uc_mcontext;
	sigset_t uc_sigmask;
};

/*
 * Advertised to guest userland via AT_MINSIGSTKSZ (asm/elf.h
 * ARCH_DLINFO): the exact bytes a signal frame occupies on the guest
 * stack (no vxrs headroom — vectors are skipped in v0).
 */
unsigned long um_minsigstksz = sizeof(struct um_rt_sigframe);

static void build_mc(struct um_mcontext *mc, struct pt_regs *regs)
{
	struct uml_pt_regs *ur = &regs->regs;
	int i;

	mc->psw.mask = ur->gp[HOST_PSW_MASK] | UM_S390_PSW_MASK_PSTATE;
	mc->psw.addr = ur->gp[HOST_PSW_ADDR];
	for (i = 0; i < 16; i++)
		mc->gprs[i] = ur->gp[HOST_GPR0 + i];
	for (i = 0; i < 8; i++) {
		unsigned long v = ur->gp[HOST_ACRS + i];

		mc->acrs[2 * i] = (unsigned int)(v >> 32);
		mc->acrs[2 * i + 1] = (unsigned int)v;
	}
	memcpy(&mc->fpregs, ur->fp, UM_S390_FP_SIZE);
}

int setup_signal_stack_si(unsigned long stack_top, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *mask)
{
	struct um_rt_sigframe __user *frame;
	struct um_rt_sigframe *kf;
	struct uml_pt_regs *ur = &regs->regs;
	int err, sig = ksig->sig;

	stack_top = round_down(stack_top, 8);
	frame = (struct um_rt_sigframe __user *)stack_top - 1;
	if (!access_ok(frame, sizeof(*frame)))
		return 1;

	kf = kzalloc(sizeof(*kf), GFP_KERNEL);
	if (!kf)
		return 1;

	kf->svc_insn = S390_SYS_SIGRETURN;
	/* uc_flags/uc_link stay zero; altstack saved below */
	build_mc(&kf->uc_mcontext, regs);
	memcpy(&kf->uc_sigmask, mask, sizeof(*mask));

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

	/* Stack backchain at frame[0] (native setup_rt_frame writes the
	 * interrupted r15 there; unwinders walk it). callee_used_stack
	 * is scratch, so this does not collide with the trampoline. */
	if (__put_user(PT_REGS_SP(regs),
		       (unsigned long __user *)&frame->callee_used_stack[0]))
		return 1;

	PT_REGS_SP(regs) = (unsigned long)frame;
	PT_REGS_IP(regs) = (unsigned long)ksig->ka.sa.sa_handler;
	ur->gp[HOST_GPR0 + 2] = sig;			/* arg1 = signo */
	ur->gp[HOST_GPR0 + 3] = (unsigned long)&frame->info;
	ur->gp[HOST_GPR0 + 4] =
		(unsigned long)&frame->uc_flags;
	/* s390 delivers the restorer in gpr14: the guest's own
	 * SA_RESTORER if it set one (CRIU, sanitizers), else the vDSO
	 * sigreturn trampoline. Native additionally sets gpr5 to the
	 * breaking-event address; UML does not track last_break, so
	 * gpr5 keeps the interrupted value. */
	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		ur->gp[HOST_GPR0 + 14] =
			(unsigned long)ksig->ka.sa.sa_restorer;
	else
		ur->gp[HOST_GPR0 + 14] = um_vdso_sigreturn;
	return 0;
}

static int restore_mc(struct pt_regs *regs,
		      struct um_mcontext __user *from)
{
	struct uml_pt_regs *ur = &regs->regs;
	struct um_mcontext mc;
	int i;

	if (copy_from_user(&mc, from, sizeof(mc)))
		return 1;

	/* Sanitize PSW: keep only user-legal bits, force problem state.
	 * The host PTRACE_SETREGSET NT_PRSTATUS is the backstop (it
	 * rejects illegal PSWs); this is defense-in-depth. */
	mc.psw.mask &= UM_S390_PSW_MASK_USER;
	mc.psw.mask |= UM_S390_PSW_MASK_PSTATE;

	ur->gp[HOST_PSW_MASK] = mc.psw.mask;
	ur->gp[HOST_PSW_ADDR] = mc.psw.addr;
	for (i = 0; i < 16; i++)
		ur->gp[HOST_GPR0 + i] = mc.gprs[i];
	for (i = 0; i < 8; i++) {
		unsigned long v =
			((unsigned long)mc.acrs[2 * i] << 32) |
			mc.acrs[2 * i + 1];

		ur->gp[HOST_ACRS + i] = v;
	}
	/* TLS lives acrs[0..1]; refresh the mirror slot */
	ur->gp[HOST_TLS] = upt_acrs_tls(ur->gp);
	memcpy(ur->fp, &mc.fpregs, UM_S390_FP_SIZE);
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

	if (restore_mc(&current->thread.regs, &frame->uc_mcontext))
		goto segfault;

	/* Avoid ERESTART handling */
	PT_REGS_SYSCALL_NR(&current->thread.regs) = -1;
	return PT_REGS_SYSCALL_RET(&current->thread.regs);

segfault:
	force_sig(SIGSEGV);
	return 0;
}
