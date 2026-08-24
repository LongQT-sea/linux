/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arm64 fault info for the UML backend: ESR_EL1 + fault address.
 * error_code carries the ESR value, trap_no its EC field, addr is the
 * faulting address (FAR/si_addr).
 */
#ifndef __FAULTINFO_ARM64_H
#define __FAULTINFO_ARM64_H

struct faultinfo {
	int error_code;		/* ESR_EL1 value */
	unsigned long addr;	/* fault address */
	int trap_no;		/* ESR EC */
};

#define INIT_FAULTINFO { 0, 0, 0 }

/* ESR_EL1 WnR (write-not-read) is bit 6 */
#define FAULT_WRITE(fi) ((fi).error_code & (1 << 6))
#define HAVE_RELIABLE_FAULT_WRITE 1
#define FAULT_ADDRESS(fi) ((fi).addr)

/*
 * Faults that demand paging can fix: instruction abort (EC 0x20 lower
 * EL / 0x21 current EL) or data abort (EC 0x24 / 0x25). Fetch faults
 * are demand paging too; without them init dies on its first text
 * page.
 */
#define SEGV_IS_FIXABLE(fi) ((((fi)->trap_no & 0x3e) == 0x20) || \
			     (((fi)->trap_no & 0x3e) == 0x24))

#define PTRACE_FULL_FAULTINFO 1

/*
 * Store the address of the fault-landing label into
 * current->thread.segv_continue, then produce 0 on the straight path,
 * 1 if a fault resumed at the label.  arm64's %0 doubles as the
 * address scratch (overwritten by the result).
 */
#define ___backtrack_faulted(_faulted)					\
	do {								\
		asm volatile (						\
			"	adr	%0, 1f\n"			\
			"	str	%0, %1\n"			\
			"	mov	%w0, #0\n"			\
			"	b	2f\n"				\
			"1:	mov	%w0, #1\n"			\
			"2:\n"						\
			: "=r" (_faulted),				\
			  "=m" (current->thread.segv_continue) ::);	\
	} while (0)

#endif
