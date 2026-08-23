/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __SKAS_H
#define __SKAS_H

#include <sysdep/ptrace.h>

extern int using_seccomp;
extern unsigned long stub_cycles_rate;

/*
 * Backend-interpreted bits copied into every stub_init_data's
 * arch_flags. A backend's boot-time host probe may set them before the
 * first stub process is exec'd (arm64: STUB_INIT_PAC_OFF).
 */
extern unsigned long stub_arch_init_flags;

/*
 * Whether the host offers PTRACE_SYSEMU, probed by check_sysemu(). Set
 * once at boot, before any stub exists. Without it the guest runs
 * under plain PTRACE_SYSCALL and each syscall is stopped at entry and
 * replaced with syscall_cancel_nr: -1, the documented "run nothing"
 * value, or a harmless substitute syscall on hosts whose seccomp
 * filter screens the number a tracer writes.
 */
extern int have_ptrace_sysemu;
extern int syscall_cancel_nr;

extern void new_thread_handler(void);
extern void handle_syscall(struct uml_pt_regs *regs);
extern unsigned long current_stub_stack(void);
extern struct mm_id *current_mm_id(void);
extern void current_mm_sync(void);
void initial_jmpbuf_lock(void);
void initial_jmpbuf_unlock(void);

#endif
