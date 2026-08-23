// SPDX-License-Identifier: GPL-2.0
/*
 * Syscall definitions for the arm64 UML backend (mirrors
 * arch/x86/um/syscalls_64.c's mmap wrapper).
 */
#include <linux/syscalls.h>
#include <linux/mm.h>

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, off)
{
	if (off & ~PAGE_MASK)
		return -EINVAL;

	return ksys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
}
