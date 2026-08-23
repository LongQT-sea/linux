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
 * as the native arm64 vDSO does.
 */
#include <linux/linkage.h>
#include <asm/unistd.h>

SYM_FUNC_START(__kernel_rt_sigreturn)
	.cfi_startproc
	.cfi_signal_frame
	mov	x8, #__NR_rt_sigreturn
	svc	#0
	.cfi_endproc
SYM_FUNC_END(__kernel_rt_sigreturn)
