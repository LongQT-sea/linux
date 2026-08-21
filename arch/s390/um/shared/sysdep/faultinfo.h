/* SPDX-License-Identifier: GPL-2.0 */
/*
 * s390x fault info for the UML backend.
 *
 * s390's fault model is the translation-exception identification
 * (TEID, in int_parm_long on the host) + si_addr — not an ESR. The
 * signal frame carries neither: si_addr is all userland gets. So:
 *   - addr  = si_addr from the siginfo (host-computed from TEID)
 *   - error_code = program interruption code (si_code-adjacent; the
 *     fixable set is {0x04 protection, 0x10/0x11/0x38..0x3b dat}).
 *   - trap_no = raw int_code incl. PER bit (0x80) for classification.
 *
 * FSI (write bit) needs facility 75 even in-kernel and never reaches
 * the signal frame, so FAULT_WRITE falls back to heuristics until a
 * ptrace-sourced TEID path exists. [DESIGN §5/F-s3]
 */
#ifndef __FAULTINFO_S390_H
#define __FAULTINFO_S390_H


struct faultinfo {
	int error_code;		/* program interruption code (masked) */
	unsigned long addr;	/* faulting address (si_addr) */
	int trap_no;		/* full int_code incl. PER flag */
};

#define INIT_FAULTINFO { 0, 0, 0 }

/*
 * Write-fault detection: the relayed si_code distinguishes store-to-
 * protected (ACCERR → classified 0x04; native s390 treats protection
 * ⇒ write, arch/s390/mm/fault.c:284-292) from unmapped (MAPERR →
 * 0x11, read). The seccomp-path fill in os-Linux/skas/process.c
 * encodes exactly that.
 */
#define HAVE_RELIABLE_FAULT_WRITE 1
#define FAULT_WRITE(fi) ((fi).error_code == 0x04)
#define FAULT_ADDRESS(fi) ((fi).addr)

/* Page-fault family: protection (0x04) and dat-exceptions
 * (0x10 asce, 0x11 segment, 0x38/0x39/0x3a/0x3b page-table/page).
 * Everything else (illegal op 0x01, spec 0x06, translate-spec 0x12,
 * ...) is a real guest bug and must be relayed, not fixed up. */
#define SEGV_IS_FIXABLE(fi)						\
	(((fi)->error_code == 0x04) ||					\
	 ((fi)->error_code >= 0x10 && (fi)->error_code <= 0x12 &&	\
	  ((fi)->error_code != 0x12)) ||				\
	 ((fi)->error_code >= 0x38 && (fi)->error_code <= 0x3b))

#define PTRACE_FULL_FAULTINFO 1

/*
 * Store the address of the fault-landing label into
 * current->thread.segv_continue, then produce 0 on the straight path,
 * 1 if a fault resumed at the label. Branch-relative-long form keeps
 * this position-independent for the stub page too.
 */
#define ___backtrack_faulted(_faulted)					\
	asm volatile (							\
		"	larl	%0, 1f\n"				\
		"	stg	%0, %1\n"				\
		"	lghi	%0, 0\n"				\
		"	j	2f\n"					\
		"1:	lghi	%0, 1\n"				\
		"2:\n"							\
		: "=d" (_faulted),					\
		  "=m" (current->thread.segv_continue) ::		\
	)

#endif
