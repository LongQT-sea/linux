// SPDX-License-Identifier: GPL-2.0
/*
 * Syscall definitions for the s390x UML backend. The generated s390
 * table maps nr 90 to sys_old_mmap (mm/mmap.c, pulled in by
 * __ARCH_WANT_SYS_OLD_MMAP); the stub-side old_mmap wrapper needs
 * sys_mmap — the one-pointer-arg form taking struct mmap_arg_struct.
 */
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/personality.h>

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

SYSCALL_DEFINE1(mmap, struct mmap_arg_struct __user *, arg)
{
	struct mmap_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	if (a.offset & ~PAGE_MASK)
		return -EINVAL;

	return ksys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd,
			       a.offset >> PAGE_SHIFT);
}

/*
 * Native s390 personality wrapper (arch/s390/kernel/syscall.c): the
 * PER_LINUX32 sticky bit survives an upgrade attempt to PER_LINUX,
 * because UML s390x is 64-bit-only and compat personalities would
 * silently change syscall decoding. Shape copied verbatim.
 */
SYSCALL_DEFINE1(s390_personality, unsigned int, personality)
{
	unsigned int ret = current->personality;

	if (personality(current->personality) == PER_LINUX32 &&
	    personality(personality) == PER_LINUX)
		personality |= PER_LINUX32;

	if (personality != 0xffffffff)
		set_personality(personality);

	if (personality(ret) == PER_LINUX32)
		ret &= ~PER_LINUX32;

	return ret;
}
