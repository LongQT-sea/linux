/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UML_LONGJMP_H
#define __UML_LONGJMP_H

#include <sysdep/archsetjmp.h>
#include <os.h>

/*
 * UML context jumps: two distinct contracts at the SUBARCH seam.
 *
 * Every UML backend implements both pairs below (asm in
 * arch/<subarch>/um/setjmp*.S, buffer layout in sysdep/archsetjmp.h).
 *
 * 1. Scheduler jump — uml_sched_jump_save()/uml_sched_jump_restore().
 *
 *    The scheduler context switch (switch_threads). The save side
 *    records EVERY callee-saved register the backend ABI permits
 *    compiled code to keep live values in — on some backends that is
 *    MORE than the integer registers:
 *
 *      * s390x: f8-f15 are callee-saved, and gcc uses them as spill
 *        slots for ordinary integer code (a pointer held live across
 *        schedule() is routinely parked in f8). GPR-only buffers
 *        silently corrupt such values; crashes surface far from the
 *        cause (NULL mm at exec, rwsem slowpath faults, kworker
 *        workqueue panics — see FINDINGS F-s20).
 *      * x86/arm64: the ABIs never keep integer values in FP/vector
 *        registers, so the scheduler-jump and relay-jump
 *        implementations are bit-identical there.
 *
 *    Restore invariant: after uml_sched_jump_restore(), every
 *    register the save side records again holds its value from the
 *    save point. Save and restore MUST run in the same host thread
 *    context; restoring into a hand-crafted buffer is not a legal
 *    use of this pair unless every recorded field was written first.
 *
 * 2. Relay/handoff restart — uml_relay_jump_save()/
 *    uml_relay_jump_restore().
 *
 *    GPR (+ip/sp) preservation only. Restores enter a context that
 *    either was saved with the same restricted save, or starts
 *    freshly from a hand-crafted buffer (IP/SP written, the rest
 *    unspecified) — therefore:
 *
 *    Invariant: NO compiled frame may hold a live value in an FP
 *    register across this jump; on resume, FP registers are
 *    UNSPECIFIED. Code that can be resumed through a relay jump
 *    must not rely on callee-saved FP registers — see the s390x
 *    warning above for what happens otherwise. In exchange, this
 *    pair may legally restore from hand-crafted buffers whose
 *    non-IP/SP fields were never initialized.
 */

extern int uml_sched_jump_save(jmp_buf *buf);
extern void uml_sched_jump_restore(jmp_buf *buf, int val)
	__attribute__((__noreturn__));
extern int uml_relay_jump_save(jmp_buf *buf);
extern void uml_relay_jump_restore(jmp_buf *buf, int val)
	__attribute__((__noreturn__));

/* Signal-state bookkeeping wraps both contracts identically. */
#define UML_SCHED_JUMP_SAVE(buf) ({			\
	int n, enable;					\
	enable = um_get_signals();			\
	n = uml_sched_jump_save(buf);			\
	if(n != 0)					\
		um_set_signals_trace(enable);		\
	n; })

#define UML_SCHED_JUMP_RESTORE(buf, val)		\
	uml_sched_jump_restore(buf, val)

#define UML_RELAY_JUMP_SAVE(buf) ({			\
	int n, enable;					\
	enable = um_get_signals();			\
	n = uml_relay_jump_save(buf);			\
	if(n != 0)					\
		um_set_signals_trace(enable);		\
	n; })

#define UML_RELAY_JUMP_RESTORE(buf, val)		\
	uml_relay_jump_restore(buf, val)

#endif
