/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __ARCH_H__
#define __ARCH_H__

#include <sysdep/ptrace.h>

extern void arch_check_bugs(void);
extern int arch_fixup(unsigned long address, struct uml_pt_regs *regs);
extern void arch_examine_signal(int sig, struct uml_pt_regs *regs);

void mc_set_rip(void *_mc, void *target);

struct seq_file;

/* Host CPU feature discovery + /proc/cpuinfo display (backend) */
void arch_parse_cpu_flags(char *line);
void arch_cpuinfo_show_extra(struct seq_file *m);

#endif
