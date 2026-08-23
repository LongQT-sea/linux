/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __PTRACE_USER_H__
#define __PTRACE_USER_H__

#include <sys/ptrace.h>
#include <sysdep/ptrace_user.h>

extern int ptrace_getregs(long pid, unsigned long *regs_out);
extern int ptrace_setregs(long pid, unsigned long *regs_in);
extern const char *ptrace_reg_name(int idx);

/* Single-word access at a user_regs_struct offset: the sysdep pair
 * (x86: PTRACE_PEEKUSER/POKEUSER; arm64: NT_PRSTATUS regsets, which is
 * all that exists there). */
extern long sysdep_ptrace_peekuser(long pid, long off, long *val);
extern long sysdep_ptrace_pokeuser(long pid, long off, long val);

#endif
