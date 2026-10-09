/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <stddef.h>
#include <sysdep/ptrace_user.h>
#include <generated/asm-offsets.h>
#include <linux/stddef.h>
#include <linux/stringify.h>
#include <asm/prctl.h>

#define STUB_MMAP_NR __NR_mmap

#define UM_SA_RESTORER 0x04000000 /* SA_RESTORER (x86 uapi) */

/* TLS restore syscall the stub filter must allow (backend-provided) */
#define STUB_TLS_SYSCALL_NR __NR_arch_prctl
#define MMAP_OFFSET(o) (o)

/* Backend mmap invocation: default direct 6-arg syscall form. */
#define STUB_MMAP_CALL(res, addr, len, prot, flags, fd, off)		\
	((res) = stub_syscall6(STUB_MMAP_NR, (addr), (len), (prot),	\
			       (flags), (fd), (off)))

#define __syscall_clobber "r11","rcx","memory"
#define __syscall "syscall"

static __always_inline long stub_syscall0(long syscall)
{
	long ret;

	__asm__ volatile (__syscall
		: "=a" (ret)
		: "0" (syscall) : __syscall_clobber );

	return ret;
}

static __always_inline long stub_syscall1(long syscall, long arg1)
{
	long ret;

	__asm__ volatile (__syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1) : __syscall_clobber );

	return ret;
}

static __always_inline long stub_syscall2(long syscall, long arg1, long arg2)
{
	long ret;

	__asm__ volatile (__syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2) : __syscall_clobber );

	return ret;
}

static __always_inline long stub_syscall3(long syscall, long arg1, long arg2,
					  long arg3)
{
	long ret;

	__asm__ volatile (__syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2), "d" (arg3)
		: __syscall_clobber );

	return ret;
}

static __always_inline long stub_syscall4(long syscall, long arg1, long arg2, long arg3,
				 long arg4)
{
	long ret;

	__asm__ volatile ("movq %5,%%r10 ; " __syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2), "d" (arg3),
		  "g" (arg4)
		: __syscall_clobber, "r10" );

	return ret;
}

static __always_inline long stub_syscall5(long syscall, long arg1, long arg2,
					  long arg3, long arg4, long arg5)
{
	long ret;

	__asm__ volatile ("movq %5,%%r10 ; movq %6,%%r8 ; " __syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2), "d" (arg3),
		  "g" (arg4), "g" (arg5)
		: __syscall_clobber, "r10", "r8" );

	return ret;
}

static __always_inline long stub_syscall6(long syscall, long arg1, long arg2,
					  long arg3, long arg4, long arg5,
					  long arg6)
{
	long ret;

	__asm__ volatile ("movq %5,%%r10 ; movq %6,%%r8 ; movq %7,%%r9 ; "
		__syscall
		: "=a" (ret)
		: "0" (syscall), "D" (arg1), "S" (arg2), "d" (arg3),
		  "g" (arg4), "g" (arg5), "g" (arg6)
		: __syscall_clobber, "r10", "r8", "r9");

	return ret;
}

static __always_inline void trap_myself(void)
{
	__asm("int3");
}

/*
 * Each stub thread runs its signal handlers on the sigaltstack inside its own
 * struct stub_data, so the data area is found from the stack pointer: the
 * first data slot follows the code page, which is found RIP-relative.
 */
static __always_inline void *get_stub_data(void)
{
	unsigned long base, sp;

	asm volatile (
		"lea 0(%%rip), %0;"
		"andq %1, %0 ;"
		"addq %2, %0 ;"
		: "=a" (base)
		: "g" (~(UM_KERN_PAGE_SIZE - 1)),
		  "g" (UM_KERN_PAGE_SIZE));
	asm volatile ("movq %%rsp, %0" : "=r" (sp));

	return (void *)(base + (sp - base) / STUB_SLOT_SIZE * STUB_SLOT_SIZE);
}

/*
 * Create a thread in the stub process that starts on @stack and calls @fn,
 * which must not return.  Returns the clone() result in the parent.
 */
#define stub_clone_thread(flags, stack, fn)				\
({									\
	long __ret;							\
	register long __r10 asm("r10") = 0;				\
	register long __r8 asm("r8") = 0;				\
									\
	asm volatile (							\
		__syscall "\n\t"					\
		"testq %%rax, %%rax\n\t"				\
		"jnz 1f\n\t"						\
		"call %P[func]\n\t"					\
		"ud2\n"							\
		"1:"							\
		: "=a" (__ret)						\
		: "0" (__NR_clone), "D" (flags), "S" (stack), "d" (0),	\
		  "r" (__r10), "r" (__r8), [func] "i" (fn)		\
		: __syscall_clobber);					\
	__ret;								\
})

/*
 * Entry instructions of the stub_exe binary, emitted from file-scope
 * asm in stub_exe.c: move the stack pointer down past the future stub
 * mappings, then call real_init().
 */
#define STUB_EXE_START							\
	"	subq	$" __stringify(STUB_SIZE) ", %rsp\n"		\
	"	call	real_init\n"

static __always_inline void
stub_seccomp_restore_state(struct stub_data_arch *arch)
{
	/*
	 * We could use _writefsbase_u64/_writegsbase_u64 if the host reports
	 * support in the hwcaps (HWCAP2_FSGSBASE).
	 */
	if (arch->sync & STUB_SYNC_FS_BASE)
		stub_syscall2(__NR_arch_prctl, ARCH_SET_FS, arch->fs_base);
	if (arch->sync & STUB_SYNC_GS_BASE)
		stub_syscall2(__NR_arch_prctl, ARCH_SET_GS, arch->gs_base);

	arch->sync = 0;
}

/*
 * x86 returns from the signal handler through the SA_RESTORER
 * trampoline (stub_signal_restorer, in the stub page), so the
 * rt_sigreturn svc is on the stub page and passes the seccomp
 * filter's IP check. Nothing to do here.
 */
static __always_inline void stub_signal_return(void *frame)
{
}

/* x86 forbids direct fs/gs base writes (arch_prctl only), so the
 * kernel already knows the TLS base; nothing for the stub to save. */
static __always_inline void stub_seccomp_save_state(struct stub_data_arch *arch)
{
}

#endif
