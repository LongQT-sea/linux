// SPDX-License-Identifier: GPL-2.0
/*
 * arch_check_bugs / arch_examine_signal for the arm64 UML backend:
 * empty (mirroring arch/x86/um/bugs_64.c; nothing to probe in v0).
 */
#include <arch.h>
#include <sysdep/ptrace.h>

void arch_check_bugs(void)
{
}

void arch_examine_signal(int sig, struct uml_pt_regs *regs)
{
}
