// SPDX-License-Identifier: GPL-2.0
/*
 * mcontext marshal for the arm64 UML backend: uml_pt_regs <-> host
 * ucontext_t (regs[31]/sp/pc/pstate), fpsimd record handling, and the
 * TPIDR_EL0 sync channel for the stub. The ESR extra-record walk lives
 * in sysdep/mcontext.h; GET_FAULTINFO_FROM_MC runs in the stub page
 * and must stay call-closed.
 */
#include <linux/errno.h>
#include <linux/string.h>
#include <sys/ucontext.h>
#include <asm/ptrace.h>
#include <sysdep/ptrace.h>
#include <sysdep/ptrace_user.h>
#include <sysdep/mcontext.h>
#include <arch.h>

#define FPSIMD_MAGIC 0x46508001
#define ESR_MAGIC 0x45535201

void get_regs_from_mc(struct uml_pt_regs *regs, mcontext_t *mc)
{
	int i;

	for (i = 0; i < 31; i++)
		regs->gp[i] = mc->regs[i];
	regs->gp[HOST_SP] = mc->sp;
	regs->gp[HOST_PC] = mc->pc;
	regs->gp[HOST_PSTATE] = mc->pstate;
}

void mc_set_rip(void *_mc, void *target)
{
	mcontext_t *mc = _mc;

	mc->pc = (unsigned long)target;
}

void get_mc_from_regs(struct uml_pt_regs *regs, mcontext_t *mc,
		      int single_stepping)
{
	int i;

	for (i = 0; i < 31; i++)
		mc->regs[i] = regs->gp[i];
	mc->sp = regs->gp[HOST_SP];
	mc->pc = regs->gp[HOST_PC];
	mc->pstate = regs->gp[HOST_PSTATE];

	/* PSTATE.SS (single-step) is bit 21 */
	if (single_stepping)
		mc->pstate |= (1UL << 21);
	else
		mc->pstate &= ~(1UL << 21);
}

/* Locate the fpsimd record's payload inside the sigframe's sigcontext */
static struct um_fpsimd_payload *get_fpstate(struct stub_data *data,
					     mcontext_t *mcontext)
{
	struct { __u32 magic; __u32 size; } *h;

	for (h = (void *)mcontext->__reserved; h->magic;
	     h = (void *)h + h->size) {
		if (h->magic == FPSIMD_MAGIC) {
			if ((void *)(h + 1) + UM_FPSIMD_PAYLOAD_SIZE >
			    (void *)data->sigstack + sizeof(data->sigstack))
				return NULL;
			return (void *)(h + 1);
		}
	}
	return NULL;
}

int get_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
		   unsigned long *fp_size_out)
{
	struct um_fpsimd_payload *sc_fp;
	struct um_fpsimd_state *fp;
	mcontext_t *mcontext;

	/* mctx_offset is verified by wait_stub_done_seccomp */
	mcontext = (void *)&data->sigstack[data->mctx_offset];

	get_regs_from_mc(regs, mcontext);

	/* trap-time x0 snapshot for UPT_SYSCALL_ARG1 (see HOST_ARG0) */
	regs->gp[HOST_ARG0] = mcontext->regs[0];

	sc_fp = get_fpstate(data, mcontext);
	if (!sc_fp)
		return -EINVAL;

	if (fp_size_out) {
		/*
		 * Size-probe call (init_seccomp sizes its allocation this
		 * way): report the size regs->fp needs and return WITHOUT
		 * copying. regs may be allocated for the bare struct here,
		 * so a copy would overrun it: on arm64 host_fp_size is
		 * pre-initialized, so the capacity check below passes and
		 * the copy would land in the heap, corrupting it.
		 */
		*fp_size_out = UM_FPSIMD_STATE_SIZE;
		return 0;
	}

	if (UM_FPSIMD_STATE_SIZE > host_fp_size)
		return -ENOSPC;

	/* sigcontext order {fpsr, fpcr, vregs} -> canonical regs->fp */
	fp = (void *)regs->fp;
	memcpy(fp->vregs, sc_fp->vregs, sizeof(fp->vregs));
	fp->fpsr = sc_fp->fpsr;
	fp->fpcr = sc_fp->fpcr;
	fp->__reserved[0] = 0;
	fp->__reserved[1] = 0;

	/* TPIDR_EL0 round trip: the value lives in the arch_data channel */
	regs->gp[HOST_TLS] = data->arch_data.tls;

	return 0;
}

int set_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
		   int single_stepping)
{
	struct um_fpsimd_payload *sc_fp;
	struct um_fpsimd_state *fp;
	mcontext_t *mcontext;

	/* mctx_offset is verified by wait_stub_done_seccomp */
	mcontext = (void *)&data->sigstack[data->mctx_offset];

	if ((unsigned long)mcontext < (unsigned long)data->sigstack ||
	    (unsigned long)mcontext >
			(unsigned long) data->sigstack +
			sizeof(data->sigstack) - sizeof(*mcontext))
		return -EINVAL;

	get_mc_from_regs(regs, mcontext, single_stepping);

	sc_fp = get_fpstate(data, mcontext);
	if (!sc_fp)
		return -EINVAL;

	/* canonical regs->fp -> sigcontext order {fpsr, fpcr, vregs} */
	fp = (void *)regs->fp;
	sc_fp->fpsr = fp->fpsr;
	sc_fp->fpcr = fp->fpcr;
	memcpy(sc_fp->vregs, fp->vregs, sizeof(sc_fp->vregs));

	/*
	 * Sync TPIDR_EL0 through the arch channel; the stub restores it
	 * via direct msr.
	 */
	if (data->arch_data.tls != regs->gp[HOST_TLS]) {
		data->arch_data.tls = regs->gp[HOST_TLS];
		data->arch_data.sync |= STUB_SYNC_TLS;
	}

	return 0;
}
