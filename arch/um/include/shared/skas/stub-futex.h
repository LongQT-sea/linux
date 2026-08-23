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
 * Hand ownership of the protocol word to the peer, publishing (release) every
 * write made before the call. Returns nonzero when the peer parked in
 * FUTEX_WAIT and a FUTEX_WAKE is actually needed; a peer that is still
 * running observes the exchange on its own.
 */
static __always_inline int
stub_futex_hand_over(volatile unsigned int *futex, unsigned int to)
{
	return stub_futex_xchg(futex, to) & STUB_FUTEX_WAITER;
}

#endif /* __STUB_FUTEX_H */
