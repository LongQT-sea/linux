/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared halves of the stub <-> kernel handoff: waiter-bit wake elision.
 * Everything here is __always_inline because the stub side must end up
 * inside .__syscall_stub with no calls out of it.
 *
 * Used from both stub_signal_interrupt() (the stub) and
 * wait_stub_done_seccomp() (the UML kernel thread). The parking step itself
 * (fetch_or of the waiter bit + FUTEX_WAIT loop) stays in the callers: the
 * two sides make the futex syscall differently and handle its failure
 * differently, and hiding that behind one helper obscures more than it saves.
 *
 * MEMORY ORDERING. The futex syscalls that used to bracket every handoff are
 * full barriers, and the whole point of this file is to skip those syscalls
 * when nobody is parked; the ordering must therefore come from the accesses
 * themselves. Every write that flips ownership is a release
 * (stub_futex_xchg), every read that decides "the peer is done" is an
 * acquire (stub_futex_load_acquire), so the data written before the flip --
 * syscall_data, signal, mctx offsets -- is visible to whoever observes the
 * flip. On a weakly ordered host (arm64) this is a real obligation: without
 * the acquire, a reader that observes the ownership flip may still read
 * stale syscall_data or signal fields, a failure x86-TSO testing
 * structurally cannot reproduce. On x86-TSO the acquire load is a plain MOV
 * plus a compiler barrier and the RMWs only add the LOCK prefix they need
 * for atomicity anyway, which is why the generic fallbacks below are already
 * optimal there.
 *
 * A backend provides its own primitives in <sysdep/stub.h> (and defines the
 * function name as a macro to itself) only when the compiler builtins would
 * emit calls out of the stub: arm64 must, because -moutline-atomics turns
 * __atomic_* into libgcc calls that land nowhere once the stub page is
 * copied to STUB_CODE. Everyone else gets the builtins, which inline on
 * every other architecture Linux runs on (x86: MOV/XCHG/LOCK CMPXCHG,
 * s390x: L/BCR serialization/CS loop).
 */
#ifndef __STUB_FUTEX_H
#define __STUB_FUTEX_H

#include <sysdep/stub.h>

#ifndef stub_futex_load_acquire
static __always_inline unsigned int
stub_futex_load_acquire(volatile unsigned int *addr)
{
	return __atomic_load_n(addr, __ATOMIC_ACQUIRE);
}

static __always_inline unsigned int
stub_futex_xchg(volatile unsigned int *addr, unsigned int val)
{
	return __atomic_exchange_n(addr, val, __ATOMIC_ACQ_REL);
}

static __always_inline unsigned int
stub_futex_fetch_or(volatile unsigned int *addr, unsigned int bits)
{
	return __atomic_fetch_or(addr, bits, __ATOMIC_ACQ_REL);
}
#endif

/*
 * Spin-hint and cycle-counter hooks for the bounded pre-park spin. A backend
 * provides them in <sysdep/stub.h> (defining each name as a macro to itself)
 * when it has a counter that is constant-rate, readable from the stub's
 * restricted context, and cheap: arm64 uses CNTVCT_EL0/CNTFRQ_EL0 with a
 * yield hint, x86 the invariant TSC with a pause hint. The fallbacks make
 * every spin budget zero, i.e. exactly the pre-spin behaviour: no polling,
 * straight to FUTEX_WAIT -- so a backend that defines nothing (e.g. s390x)
 * is unaffected, while the timing-free waiter-bit wake elision still applies.
 *
 * stub_cycles() runs inside the stub; stub_cycles_per_us() is only ever
 * called on the host side (check_stub_cycles() probes both in a sacrificial
 * child first, so a host that traps the counter read costs one process, not
 * the boot). Returning 0 from stub_cycles_per_us() means "no usable rate"
 * and disables the spin.
 */
#ifndef stub_relax
static __always_inline void stub_relax(void)
{
	/* Just a compiler barrier, so the polling loop re-reads memory. */
	__asm__ volatile("" ::: "memory");
}
#endif

#ifndef stub_cycles
static __always_inline unsigned long stub_cycles(void)
{
	return 0;
}

static __always_inline unsigned long stub_cycles_per_us(void)
{
	return 0;
}
#endif

/* Streak cap only guards against wraparound; any small value works. */
#define STUB_SPIN_STREAK_MAX	16
/* Waits between spin probes while the spin keeps missing. */
#define STUB_SPIN_BACKOFF	16

/*
 * Hand ownership of the protocol word to the peer, publishing (release) every
 * write made before the call. Returns nonzero when the peer parked in
 * FUTEX_WAIT and a FUTEX_WAKE is actually needed; a peer caught spinning (or
 * still running) observes the exchange on its own.
 */
static __always_inline int
stub_futex_hand_over(volatile unsigned int *futex, unsigned int to)
{
	return stub_futex_xchg(futex, to) & STUB_FUTEX_WAITER;
}

/*
 * How long this wait may spin, in stub_cycles() ticks. Zero means "go park".
 */
static __always_inline unsigned long
stub_futex_spin_budget(struct stub_spin_state *s, unsigned int spin_ticks)
{
	if (!spin_ticks)
		return 0;

	if (s->streak)
		return spin_ticks;

	/* Recent miss: sleep for a while before probing the spin again. */
	if (s->backoff) {
		s->backoff--;
		return 0;
	}

	return spin_ticks;
}

/* Record the outcome of an actually-attempted spin. */
static __always_inline void
stub_futex_spin_result(struct stub_spin_state *s, int hit)
{
	if (hit) {
		if (s->streak < STUB_SPIN_STREAK_MAX)
			s->streak++;
	} else {
		s->streak = 0;
		s->backoff = STUB_SPIN_BACKOFF;
	}
}

/*
 * Poll the word until ownership leaves `while_owner`, for at most `ticks`
 * ticks of wall time. Returns the last value observed, read with acquire
 * semantics, so on a hit the caller may immediately read the peer's data.
 *
 * The bound is wall time by construction, never an iteration count: the
 * budget must mean the same thing at the host governor's frequency floor and
 * its ceiling, and only a constant-rate counter delivers that. The unsigned
 * subtraction also makes a counter anomaly (wraparound, or an offset after a
 * cross-socket migration on x86) fail towards "budget spent", never towards
 * an unbounded spin.
 */
static __always_inline unsigned int
stub_futex_spin(volatile unsigned int *futex, unsigned int while_owner,
		unsigned long ticks)
{
	unsigned int val = stub_futex_load_acquire(futex);
	unsigned long start;

	if (STUB_FUTEX_OWNER(val) != while_owner || !ticks)
		return val;

	start = stub_cycles();
	do {
		stub_relax();
		val = stub_futex_load_acquire(futex);
		if (STUB_FUTEX_OWNER(val) != while_owner)
			break;
	} while (stub_cycles() - start < ticks);

	return val;
}

#endif /* __STUB_FUTEX_H */
