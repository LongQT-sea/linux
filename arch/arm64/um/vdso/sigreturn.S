/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The signal-return trampoline: the guest kernel points x30 here when
 * delivering a signal (arm64 has no SA_RESTORER).  Real assembly, not a
 * naked C function: aarch64 GCC accepts the naked attribute only under
 * restrictions and warns-ignores it under others, leaving prologue
 * emission to opt-level luck, and a prologue would clobber the
 * sigframe this runs on.
 *
 * .cfi_signal_frame marks this as a signal trampoline for unwinders,
 * as the native arm64 vDSO does.  The .cfi_offset directives are what
 * let gdb and libunwind walk out of a signal handler and back into the
 * interrupted frame: the frame base is sp, and the saved registers
 * live in the sigcontext at a fixed offset from it, mirroring
 * arch/arm64/kernel/vdso/sigreturn.S.
 */
#include <linux/linkage.h>
#include <asm/unistd.h>

/*
 * Offsets into the rt_sigframe that the unwinder needs.  They cannot be
 * computed here, so they are literals, and um_sigframe_abi_check() in
 * arch/arm64/um/signal.c pins each one against the real struct offsets
 * of setup_signal_stack_si()'s frame:
 *	sp -> struct um_rt_sigframe { siginfo info; ucontext fields; sc }
 * with the register array inside sc.
 */
#define SIGFRAME_SIGINFO_SIZE	128	/* sizeof(siginfo_t) */
#define UCONTEXT_MCONTEXT_OFF	176	/* uc start to sigcontext */
#define SIGCONTEXT_REGS_OFF	8	/* offsetof(um_sigcontext, regs) */
#define SIGFRAME_REGS_OFF	(SIGFRAME_SIGINFO_SIZE + \
				 UCONTEXT_MCONTEXT_OFF + SIGCONTEXT_REGS_OFF)

	.text

	.cfi_startproc
	.cfi_signal_frame
	.cfi_def_cfa	sp, 0
	.cfi_offset	x0,  SIGFRAME_REGS_OFF + 0 * 8
	.cfi_offset	x1,  SIGFRAME_REGS_OFF + 1 * 8
	.cfi_offset	x2,  SIGFRAME_REGS_OFF + 2 * 8
	.cfi_offset	x3,  SIGFRAME_REGS_OFF + 3 * 8
	.cfi_offset	x4,  SIGFRAME_REGS_OFF + 4 * 8
	.cfi_offset	x5,  SIGFRAME_REGS_OFF + 5 * 8
	.cfi_offset	x6,  SIGFRAME_REGS_OFF + 6 * 8
	.cfi_offset	x7,  SIGFRAME_REGS_OFF + 7 * 8
	.cfi_offset	x8,  SIGFRAME_REGS_OFF + 8 * 8
	.cfi_offset	x9,  SIGFRAME_REGS_OFF + 9 * 8
	.cfi_offset	x10, SIGFRAME_REGS_OFF + 10 * 8
	.cfi_offset	x11, SIGFRAME_REGS_OFF + 11 * 8
	.cfi_offset	x12, SIGFRAME_REGS_OFF + 12 * 8
	.cfi_offset	x13, SIGFRAME_REGS_OFF + 13 * 8
	.cfi_offset	x14, SIGFRAME_REGS_OFF + 14 * 8
	.cfi_offset	x15, SIGFRAME_REGS_OFF + 15 * 8
	.cfi_offset	x16, SIGFRAME_REGS_OFF + 16 * 8
	.cfi_offset	x17, SIGFRAME_REGS_OFF + 17 * 8
	.cfi_offset	x18, SIGFRAME_REGS_OFF + 18 * 8
	.cfi_offset	x19, SIGFRAME_REGS_OFF + 19 * 8
	.cfi_offset	x20, SIGFRAME_REGS_OFF + 20 * 8
	.cfi_offset	x21, SIGFRAME_REGS_OFF + 21 * 8
	.cfi_offset	x22, SIGFRAME_REGS_OFF + 22 * 8
	.cfi_offset	x23, SIGFRAME_REGS_OFF + 23 * 8
	.cfi_offset	x24, SIGFRAME_REGS_OFF + 24 * 8
	.cfi_offset	x25, SIGFRAME_REGS_OFF + 25 * 8
	.cfi_offset	x26, SIGFRAME_REGS_OFF + 26 * 8
	.cfi_offset	x27, SIGFRAME_REGS_OFF + 27 * 8
	.cfi_offset	x28, SIGFRAME_REGS_OFF + 28 * 8
	.cfi_offset	x29, SIGFRAME_REGS_OFF + 29 * 8
	.cfi_offset	x30, SIGFRAME_REGS_OFF + 30 * 8
	.cfi_offset	sp,  SIGFRAME_REGS_OFF + 31 * 8

SYM_FUNC_START(__kernel_rt_sigreturn)
	mov	x8, #__NR_rt_sigreturn
	svc	#0
SYM_FUNC_END(__kernel_rt_sigreturn)
	.cfi_endproc
