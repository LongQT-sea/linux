/* SPDX-License-Identifier: GPL-2.0 */
/*

 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2005 Jeff Dike (jdike@karaya.com)
 */

#ifndef __STUB_DATA_H
#define __STUB_DATA_H

#include <linux/compiler_types.h>
#include <as-layout.h>
#include <sysdep/tls.h>
#include <sysdep/stub-data.h>
#include <mm_id.h>

/*
 * The stub <-> kernel handoff word. The low bit names the side that currently
 * owns the CPU ("the child may run" / "the kernel may run"); ownership strictly
 * ping-pongs, so at any moment at most one side is waiting.
 */
#define FUTEX_IN_CHILD 0
#define FUTEX_IN_KERN 1
/*
 * ORed in by a waiter that is about to park in FUTEX_WAIT. A waker hands
 * ownership over with an atomic exchange and calls FUTEX_WAKE only if the old
 * value carried this bit: a FUTEX_WAKE that actually has someone to wake costs
 * a voluntary context switch on the host, and even an empty one is a syscall.
 * When the peer has not parked (yet), the wake is pure waste.
 *
 * The bit can go stale in one benign way: the waiter sets it just after the
 * waker's exchange already flipped ownership. The waiter then sees the flip in
 * the fetch_or's old value and never parks, but the bit stays set until the
 * next exchange clears it, costing that exchange one spurious FUTEX_WAKE.
 * Rare and harmless; a scheme that cleans the bit up would need a second
 * atomic on every wait, which is the common path.
 */
#define STUB_FUTEX_WAITER 2

/* The ownership half of the protocol word, waiter bit masked off. */
#define STUB_FUTEX_OWNER(v) ((v) & FUTEX_IN_KERN)

struct stub_init_data {
	int seccomp;

	/*
	 * Backend-interpreted bits, consumed by stub_arch_init() in the
	 * freshly exec'd stub before the seccomp filter is installed.
	 * Filled from stub_arch_init_flags, which a backend's host
	 * probe may set at boot (arm64: STUB_INIT_PAC_OFF).
	 */
	unsigned long arch_flags;

	unsigned long stub_start;

	int stub_code_fd;
	unsigned long stub_code_offset;
	int stub_data_fd;
	unsigned long stub_data_offset;

	unsigned long signal_handler;
	unsigned long signal_restorer;
};

#define STUB_NEXT_SYSCALL(s) \
	((struct stub_syscall *) (((unsigned long) s) + (s)->cmd_len))

enum stub_syscall_type {
	STUB_SYSCALL_UNSET = 0,
	STUB_SYSCALL_MMAP,
	STUB_SYSCALL_MUNMAP,
};

struct stub_syscall {
	struct {
		unsigned long addr;
		unsigned long length;
		unsigned long offset;
		int fd;
		int prot;
	} mem;

	enum stub_syscall_type syscall;
};

struct stub_data {
	long err;

	int syscall_data_len;
	/* 128 leaves enough room for additional fields in the struct */
	struct stub_syscall syscall_data[(UM_KERN_PAGE_SIZE - 128) / sizeof(struct stub_syscall)] __aligned(16);

	/* data shared with signal handler (only used in seccomp mode) */
	short restart_wait;
	unsigned int futex;
	int signal;
	unsigned short si_offset;
	unsigned short mctx_offset;

	/* seccomp architecture specific state restore */
	struct stub_data_arch arch_data;

	/* Stack for our signal handlers and for calling into .
	 * Sized per backend: arm64's signal frame exceeds one page. */
	unsigned char sigstack[UM_STUB_SIGSTACK_PAGES * UM_KERN_PAGE_SIZE] __aligned(UM_KERN_PAGE_SIZE);
};

#endif
