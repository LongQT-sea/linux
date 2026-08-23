// SPDX-License-Identifier: GPL-2.0
/*
 * System call table for UML/arm64: the generated generic table with
 * the UML syscall-pointer shape, ni-filled for the gaps.
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
#define __SYSCALL_WITH_COMPAT(nr, native, compat) __SYSCALL(nr, native)
#include <asm/syscall_table_64.h>
#undef  __SYSCALL
#undef  __SYSCALL_WITH_COMPAT

#define __SYSCALL(nr, sym) \
	[nr] = sym,
#define __SYSCALL_WITH_COMPAT(nr, native, compat) __SYSCALL(nr, native)

/* Not yet implemented: the arm64 signal frame is the next surface. */
#define sys_rt_sigreturn sys_ni_syscall

const sys_call_ptr_t sys_call_table[] ____cacheline_aligned = {
	[0 ... __NR_syscalls - 1] = sys_ni_syscall,
#include <asm/syscall_table_64.h>
};

int syscall_table_size = sizeof(sys_call_table);
