/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linker script for the arm64 vDSO: version script plus the layout
 * include. No x86-style prelink address: UML maps the page at
 * task_size - PAGE_SIZE regardless (see vma.c).
 */

#define VDSO_PRELINK 0x0
#include "vdso-layout.lds.S"

/*
 * This controls what userland symbols we export from the vDSO.
 */
VERSION {
	LINUX_2.6 {
	global:
		clock_gettime;
		__vdso_clock_gettime;
		gettimeofday;
		__vdso_gettimeofday;
		__kernel_rt_sigreturn;
	local: *;
	};
}

VDSO64_PRELINK = VDSO_PRELINK;
