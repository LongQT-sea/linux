/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_X86_UPROBES_GLUE_H
#define __UM_X86_UPROBES_GLUE_H

/*
 * Glue for building arch/x86/kernel/uprobes.c as a UML subarch object.
 * um's pt_regs wraps struct uml_pt_regs and lacks x86's named register
 * fields (the shared file uses the PT_REGS accessors under CONFIG_UML),
 * and UML/x86 is 64-bit only. Only what the shared file references is
 * defined here.
 */

struct pt_regs;
struct mm_struct;

/* UML/x86-64 has no ia32 compat mode (CONFIG_IA32_EMULATION is off) */
#define user_64bit_mode(regs)	true
#define is_64bit_mm(mm)		true

#endif /* __UM_X86_UPROBES_GLUE_H */
