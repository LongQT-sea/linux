/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The signal-return trampoline: the guest kernel points gprs[14] here
 * when delivering a signal (s390 SA_RESTORER convention). Real
 * assembly, not a naked C function: a prologue would clobber the
 * sigframe this runs on. svc 0 with r1 = __NR_rt_sigreturn is the
 * native s390 calling shape.
 */
#include <linux/linkage.h>
#include <asm/unistd.h>

SYM_FUNC_START(__kernel_rt_sigreturn)
	lghi	%r1, __NR_rt_sigreturn
	svc	0
SYM_FUNC_END(__kernel_rt_sigreturn)
