// SPDX-License-Identifier: GPL-2.0
/*
 * mcontext marshal for the s390x UML backend: uml_pt_regs <-> host
 * ucontext_t (psw, gprs, acrs, fpregs — flat, layout-identical to
 * _sigregs per F-s1), fixed 136B FP frame handling, and the acrs TLS
 * channel. No record walk exists on s390 to mis-parse.
 */
#include <linux/errno.h>
#include <signal.h>
#include <linux/string.h>
#include <sys/ucontext.h>
#include <asm/ptrace.h>
#include <sysdep/ptrace.h>
#include <sysdep/ptrace_user.h>
#include <sysdep/mcontext.h>
#include <arch.h>

void get_regs_from_mc(struct uml_pt_regs *regs, mcontext_t *mc)
{
	int i;

	regs->gp[HOST_PSW_MASK] = mc->psw.mask;
	regs->gp[HOST_PSW_ADDR] = mc->psw.addr;
	for (i = 0; i < 16; i++)
		regs->gp[HOST_GPR0 + i] = mc->gregs[i];
	/* acrs are 32-bit halves inside long slots: pack pairs of
	 * big-endian halves into the frame's HOST_ACRS long slot. */
	for (i = 0; i < 8; i++) {
		unsigned long v =
			((unsigned long)mc->aregs[2 * i] << 32) |
			mc->aregs[2 * i + 1];
		regs->gp[HOST_ACRS + i] = v;
	}
}

void mc_set_rip(void *_mc, void *target)
{
	mcontext_t *mc = _mc;

	mc->psw.addr = (unsigned long)target;
}

void get_mc_from_regs(struct uml_pt_regs *regs, mcontext_t *mc,
		      int single_stepping)
{
	int i;

	mc->psw.mask = regs->gp[HOST_PSW_MASK];
	mc->psw.addr = regs->gp[HOST_PSW_ADDR];
	for (i = 0; i < 16; i++)
		mc->gregs[i] = regs->gp[HOST_GPR0 + i];
	for (i = 0; i < 8; i++) {
		unsigned long v = regs->gp[HOST_ACRS + i];

		mc->aregs[2 * i] = (unsigned int)(v >> 32);
		mc->aregs[2 * i + 1] = (unsigned int)v;
	}

	/* PSW PER bit (single-step) is bit 63-1: 0x4000000000000000 */
	if (single_stepping)
		mc->psw.mask |= (1UL << 62);
	else
		mc->psw.mask &= ~(1UL << 62);
}

int get_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
		   unsigned long *fp_size_out)
{
	mcontext_t *mcontext;

	/* mctx_offset is verified by wait_stub_done_seccomp */
	mcontext = (void *)&data->sigstack[data->mctx_offset];

	get_regs_from_mc(regs, mcontext);

	if (fp_size_out) {
		/*
		 * Size-probe call (init_seccomp sizes its allocation this
		 * way): report the frame size and return WITHOUT copying.
		 * The s390 fp frame is a compile-time constant, so unlike
		 * x86 there is no dynamic discovery here.
		 */
		*fp_size_out = UM_S390_FP_SIZE;
		return 0;
	}

	memcpy(&regs->fp, &mcontext->fpregs, UM_S390_FP_SIZE);

	/*
	 * The mcontext has no orig_gpr2 and the host kernel clobbers
	 * gprs[2] to -ENOSYS after seccomp's rollback
	 * (arch/s390/kernel/syscall.c:134), so the signal frame can
	 * never carry arg1 on s390. The tracer-side drain stashed the
	 * true arg1 (orig_gpr2 at the SIGSYS delivery-stop) in
	 * stub_data before reinjecting; consume it here.
	 *
	 * Keyed on arg1_valid alone: data->signal is set by the stub
	 * handler concurrently with UML touching the page (the drain
	 * injects during our round), so it is not a reliable marker.
	 * arg1_valid is set strictly before that injection and
	 * consumed exactly once per relayed syscall, so it cannot
	 * alias across rounds. Non-SIGSYS rounds (faults, async
	 * signals) never have it set: leave ORIG_GPR2 as-is.
	 */
	if (data->arg1_valid) {
		regs->gp[HOST_ORIG_GPR2] = data->relay_arg1;
		/*
		 * NOTE: no PSW advance here. The seccomp rollback
		 * (arch/s390/kernel/syscall.c) already leaves the
		 * interrupted PSW AFTER the svc — box-proven twice:
		 * the exit(30) handshake stop showed psw=svc+2 and the
		 * guest brk stop showed psw=svc+2; adding 2 more
		 * resumed the guest mid-instruction (SIGILL at
		 * ld.so's brk return point). F-s6's "delta=0" measured
		 * the pre-rollback frame, not the delivery stop.
		 */
	}
	/* int_code mirror: glibc does not carry it in mcontext_t; the
	 * syscall number reaches us through si_syscall at the caller. */

	return 0;
}

int set_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
		   int single_stepping)
{
	mcontext_t *mcontext;

	/* mctx_offset is verified by wait_stub_done_seccomp */
	mcontext = (void *)&data->sigstack[data->mctx_offset];

	if ((unsigned long)mcontext < (unsigned long)data->sigstack ||
	    (unsigned long)mcontext >
			(unsigned long) data->sigstack +
			sizeof(data->sigstack) - sizeof(*mcontext))
		return -EINVAL;

	get_mc_from_regs(regs, mcontext, single_stepping);

	memcpy(&mcontext->fpregs, &regs->fp, UM_S390_FP_SIZE);

	/*
	 * Acrs ride the frame itself — nothing outside the mcontext to
	 * sync (unlike x86 fs_base/gs_base arch_prctl or arm64 TPIDR).
	 */

	return 0;
}
