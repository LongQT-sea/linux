/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <stddef.h>
#include <asm/ptrace.h>
#include <generated/asm-offsets.h>
#include <linux/stringify.h>

#define STUB_MMAP_NR __NR_mmap2

#define UM_SA_RESTORER 0x04000000 /* SA_RESTORER (x86 uapi) */

/* TLS restore syscall the stub filter must allow (backend-provided) */
#define STUB_TLS_SYSCALL_NR __NR_set_thread_area
#define MMAP_OFFSET(o) ((o) >> UM_KERN_PAGE_SHIFT)

/* Backend mmap invocation: default direct 6-arg syscall form. */
#define STUB_MMAP_CALL(res, addr, len, prot, flags, fd, off)		\
	((res) = stub_syscall6(STUB_MMAP_NR, (addr), (len), (prot),	\
			       (flags), (fd), (off)))

static __always_inline long stub_syscall0(long syscall)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall1(long syscall, long arg1)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall2(long syscall, long arg1, long arg2)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall3(long syscall, long arg1, long arg2,
					  long arg3)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2), "d" (arg3)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall4(long syscall, long arg1, long arg2,
					  long arg3, long arg4)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2), "d" (arg3), "S" (arg4)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall5(long syscall, long arg1, long arg2,
					  long arg3, long arg4, long arg5)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "0" (syscall), "b" (arg1),
			"c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5)
			: "memory");

	return ret;
}

static __always_inline long stub_syscall6(long syscall, long arg1, long arg2,
					  long arg3, long arg4, long arg5,
					  long arg6)
{
	struct syscall_args {
		int ebx, ebp;
	} args = { arg1, arg6 };
	long ret;

	__asm__ volatile ("pushl %%ebp;"
			"movl 0x4(%%ebx),%%ebp;"
			"movl (%%ebx),%%ebx;"
			"int $0x80;"
			"popl %%ebp"
			: "=a" (ret)
			: "0" (syscall), "b" (&args),
			"c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5)
			: "memory");

	return ret;
}

static __always_inline void trap_myself(void)
{
	__asm("int3");
}

static __always_inline void *get_stub_data(void)
{
	unsigned long ret;

	asm volatile (
		"call _here_%=;"
		"_here_%=:"
		"popl %0;"
		"andl %1, %0 ;"
		"addl %2, %0 ;"
		: "=a" (ret)
		: "g" (~(UM_KERN_PAGE_SIZE - 1)),
		  "g" (UM_KERN_PAGE_SIZE));

	return (void *)ret;
}

/*
 * Entry instructions of the stub_exe binary, emitted from file-scope
 * asm in stub_exe.c: move the stack pointer down past the future stub
 * mappings, then call real_init().
 */
#define STUB_EXE_START							\
	"	subl	$" __stringify(STUB_SIZE) ", %esp\n"		\
	"	call	real_init\n"

static __always_inline void
stub_seccomp_restore_state(struct stub_data_arch *arch)
{
	for (int i = 0; i < sizeof(arch->tls) / sizeof(arch->tls[0]); i++) {
		if (arch->sync & (1 << i))
			stub_syscall1(__NR_set_thread_area,
				      (unsigned long) &arch->tls[i]);
	}

	arch->sync = 0;
}

/* See stub_64.h: x86 returns via the SA_RESTORER trampoline. */
static __always_inline void stub_signal_return(void *frame)
{
}

/* See stub_64.h: x86 learns TLS via arch_prctl; nothing to save. */
static __always_inline void stub_seccomp_save_state(struct stub_data_arch *arch)
{
}

#endif
