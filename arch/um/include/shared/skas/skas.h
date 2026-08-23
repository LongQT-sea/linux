/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __SKAS_H
#define __SKAS_H

#include <sysdep/ptrace.h>

extern int using_seccomp;

/*
 * Backend-interpreted bits copied into every stub_init_data's
 * arch_flags. A backend's boot-time host probe may set them before the
 * first stub process is exec'd (arm64: STUB_INIT_PAC_OFF).
 */
extern unsigned long stub_arch_init_flags;

extern void new_thread_handler(void);
extern void handle_syscall(struct uml_pt_regs *regs);
extern unsigned long current_stub_stack(void);
extern struct mm_id *current_mm_id(void);
extern void current_mm_sync(void);
void initial_jmpbuf_lock(void);
void initial_jmpbuf_unlock(void);

#endif
