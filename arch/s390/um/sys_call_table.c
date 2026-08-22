// SPDX-License-Identifier: GPL-2.0
/*
 * System call table for UML/s390x — the generated s390 table with the
 * UML syscall-pointer shape, ni-filled for the gaps.
 */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <linux/cache.h>
#include <asm/unistd.h>
#include <asm/syscall.h>

extern asmlinkage long sys_ni_syscall(unsigned long, unsigned long,
				      unsigned long, unsigned long,
				      unsigned long, unsigned long);

#define __SYSCALL_NORETURN __SYSCALL

#define __SYSCALL(nr, sym) extern asmlinkage long sym(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);
#include "syscall_aliases.h"
#include <asm/syscall_table.h>
#undef  __SYSCALL

#define __SYSCALL(nr, sym) [nr] = sym,

const sys_call_ptr_t sys_call_table[] ____cacheline_aligned = {
	[0 ... __NR_syscalls - 1] = sys_ni_syscall,
	#include "syscall_aliases.h"
#include <asm/syscall_table.h>
};
