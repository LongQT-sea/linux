/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2004 PathScale, Inc
 */

#ifndef __REGISTERS_H
#define __REGISTERS_H

#include <sysdep/ptrace.h>

extern unsigned long exec_regs[UM_GP_SLOTS];
extern unsigned long *exec_fp_regs;

extern int init_pid_registers(int pid);
extern void get_safe_registers(unsigned long *regs, unsigned long *fp_regs);
extern int get_fp_registers(int pid, unsigned long *regs);
extern int put_fp_registers(int pid, unsigned long *regs);

/*
 * Per-backend hook: a guest SIGILL may be an emulated instruction
 * (arm64: PAC instructions, which FPAC-fault when a forked child's
 * stub authenticates parent-signed pointers with its own host keys).
 * Return non-zero when the instruction was emulated and the signal
 * must not be relayed. Always 0 on hosts without such instructions.
 */
extern int arch_sigill_fixup(struct uml_pt_regs *regs);

#endif
