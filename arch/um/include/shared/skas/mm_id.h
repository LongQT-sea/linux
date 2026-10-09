/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005 Jeff Dike (jdike@karaya.com)
 */

#ifndef __MM_ID_H
#define __MM_ID_H

#include <linux/compiler_types.h>

#define STUB_MAX_FDS 4

struct mm_id;

/*
 * A host thread of a stub process, with its data area (struct stub_data,
 * mapped at STUB_SLOT_DATA(slot) in the stub) and its queue of pending
 * mmap/munmap requests.
 */
struct stub_thread {
	struct mm_id *mm;
	int tid;
	int slot;
	unsigned long stack;
	int syscall_data_len;

	/* Only used with SECCOMP mode */
	int syscall_fd_num;
	int syscall_fd_map[STUB_MAX_FDS];
};

struct mm_id {
	int pid;

	/* Only used with SECCOMP mode */
	int sock;

	/*
	 * The thread that exec()ed the stub. Without CONFIG_UML_MT_STUB (and
	 * in ptrace mode) it runs userspace. With it, it only services
	 * requests for the whole process (creating threads, mapping changes).
	 */
	struct stub_thread main;

#ifdef CONFIG_UML_MT_STUB
	/* Per-CPU threads that run userspace, created on first use */
	struct stub_thread *workers[CONFIG_NR_CPUS];
#endif
};

struct mutex *__get_turnstile(struct mm_id *mm_id);
void enter_turnstile(struct mm_id *mm_id) __acquires(__get_turnstile(mm_id));
void exit_turnstile(struct mm_id *mm_id) __releases(__get_turnstile(mm_id));

void notify_mm_kill(int pid);

struct stub_thread *enter_userspace(struct mm_id *mm_id);
void userspace_failed(struct mm_id *mm_id, struct stub_thread *st);
void um_stub_kill(struct mm_id *mm_id);
void leave_userspace(struct mm_id *mm_id, struct stub_thread *st);

#endif
