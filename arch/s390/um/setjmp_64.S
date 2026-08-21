// SPDX-License-Identifier: GPL-2.0
/*
 * UML context jumps for s390x — two contracts (see
 * arch/um/include/shared/longjmp.h):
 *
 * Scheduler jump (uml_sched_jump_*): full callee-saved state.
 *   r6..r13 @0..56, r14 @64, r15 @72, f8..f15 @80..136.
 *   s390x gcc spills ordinary integer values into f8-f15 (they are
 *   callee-saved), so the scheduler switch MUST record them. A
 *   GPR-only scheduler buffer resumes with corrupted pointers:
 *   NULL mm at exec, rwsem slowpath faults, kworker panics (F-s20).
 *
 * Relay jump (uml_relay_jump_*): GPR (+ip/sp) only.
 *   r6..r15 layout identical to above; FP registers UNSPECIFIED on
 *   resume by contract. Used where a hand-crafted buffer (only
 *   IP/SP written) is a legal restore source or the caller holds no
 *   live FP state across the jump.
 *
 *   (History: the original .S stored r14@72/r15@80, one slot off,
 *   so longjmp restored r14 from the SP written by
 *   start_idle_thread and executed the stack: SIGILL ILL_ILLOPC at
 *   exactly UM_THREAD_START_SP.)
 */
#include <linux/linkage.h>

.text

/* int uml_sched_jump_save(jmp_buf *buf) — r2 = &buf[0]; returns 0 */
SYM_FUNC_START(uml_sched_jump_save)
	stmg	%r6,%r13,0(%r2)		/* r6..r13 @0..56 */
	stg	%r14,64(%r2)		/* return address */
	stg	%r15,72(%r2)		/* stack pointer */
	std	%f8,80(%r2)
	std	%f9,88(%r2)
	std	%f10,96(%r2)
	std	%f11,104(%r2)
	std	%f12,112(%r2)
	std	%f13,120(%r2)
	std	%f14,128(%r2)
	std	%f15,136(%r2)
	lghi	%r2,0
	br	%r14
SYM_FUNC_END(uml_sched_jump_save)

/*
 * void uml_sched_jump_restore(jmp_buf *buf, int val)
 * — r2 = &buf[0], r3 = val; noreturn; resumes at the save point
 * with val as setjmp's return (1 if val == 0).
 */
SYM_FUNC_START(uml_sched_jump_restore)
	lgr	%r5,%r2			/* buf */
	lgfr	%r2,%r3			/* return value = val */
	ltgr	%r2,%r2
	jne	0f
	lghi	%r2,1			/* restore(x,0) returns 1 */
0:	lmg	%r6,%r13,0(%r5)
	ld	%f8,80(%r5)
	ld	%f9,88(%r5)
	ld	%f10,96(%r5)
	ld	%f11,104(%r5)
	ld	%f12,112(%r5)
	ld	%f13,120(%r5)
	ld	%f14,128(%r5)
	ld	%f15,136(%r5)
	lg	%r14,64(%r5)
	lg	%r15,72(%r5)
	br	%r14
SYM_FUNC_END(uml_sched_jump_restore)

/* int uml_relay_jump_save(jmp_buf *buf) — GPR-only; returns 0 */
SYM_FUNC_START(uml_relay_jump_save)
	stmg	%r6,%r13,0(%r2)		/* r6..r13 @0..56 */
	stg	%r14,64(%r2)		/* return address */
	stg	%r15,72(%r2)		/* stack pointer */
	lghi	%r2,0
	br	%r14
SYM_FUNC_END(uml_relay_jump_save)

/* void uml_relay_jump_restore(jmp_buf *buf, int val) — GPR only */
SYM_FUNC_START(uml_relay_jump_restore)
	lgr	%r5,%r2			/* buf */
	lgfr	%r2,%r3			/* return value = val */
	ltgr	%r2,%r2
	jne	0f
	lghi	%r2,1			/* restore(x,0) returns 1 */
0:	lmg	%r6,%r13,0(%r5)
	lg	%r14,64(%r5)
	lg	%r15,72(%r5)
	br	%r14
SYM_FUNC_END(uml_relay_jump_restore)

/*
 * Legacy names — the historical kernel_setjmp/kernel_longjmp (and
 * their -Dsetjmp/-Dlongjmp call-site spellings) are the RELAY
 * contract (GPR-only). New code should use the named
 * uml_relay_jump_save / uml_sched_jump_save etc above.
 */
SYM_FUNC_ALIAS(setjmp, uml_relay_jump_save)
SYM_FUNC_ALIAS(longjmp, uml_relay_jump_restore)
SYM_FUNC_ALIAS(kernel_setjmp, uml_relay_jump_save)
SYM_FUNC_ALIAS(kernel_longjmp, uml_relay_jump_restore)
SYM_FUNC_ALIAS(kernel_setjmp, uml_relay_jump_save)
SYM_FUNC_ALIAS(kernel_longjmp, uml_relay_jump_restore)
