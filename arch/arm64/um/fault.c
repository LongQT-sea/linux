// SPDX-License-Identifier: GPL-2.0
/*
 * Exception-table fixup for the arm64 UML backend, identical
 * mechanism to arch/x86/um/fault.c (UML's extable is its own format;
 * the fixup simply rewrites IP).
 */

#include <arch.h>
#include <sysdep/ptrace.h>

/* These two are from asm-um/uaccess.h and linux/module.h, check them. */
struct exception_table_entry {
	unsigned long insn;
	unsigned long fixup;
};

const struct exception_table_entry *search_exception_tables(unsigned long add);

int arch_fixup(unsigned long address, struct uml_pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(address);
	if (fixup) {
		UPT_IP(regs) = fixup->fixup;
		return 1;
	}
	return 0;
}
