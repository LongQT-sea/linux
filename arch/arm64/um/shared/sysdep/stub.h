/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The stub execution contract for the arm64 UML backend:
 * svc #0 syscall sequences (x8 = nr, x0..x5 = args), brk #0 as the
 * trap-back notifier, adrp-based stub_data locator, the stub_exe
 * entry, and in-stub TLS restore via direct msr TPIDR_EL0
 * (EL0-writable, no arch_prctl round trip needed).
 */
#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <stddef.h>
#include <asm/unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <as-layout.h>
#include <stub-data.h>
#include <sysdep/ptrace_user.h>
#include <generated/asm-offsets.h>
#include <linux/stddef.h>
#include <linux/stringify.h>

#define STUB_MMAP_NR __NR_mmap

#define UM_SA_RESTORER 0 /* arm64 has no SA_RESTORER; the kernel provides the sigreturn trampoline */

/* TLS restore syscall the stub filter must allow (backend-provided) */
#define STUB_TLS_SYSCALL_NR __NR_set_tls
#define MMAP_OFFSET(o) (o)

/* Backend mmap invocation: the direct 6-argument syscall form. */
#define STUB_MMAP_CALL(res, addr, len, prot, flags, fd, off)		\
	((res) = stub_syscall6(STUB_MMAP_NR, (addr), (len), (prot),	\
			       (flags), (fd), (off)))

#define __syscall_clobber "memory"

static __always_inline long stub_syscall0(long syscall)
{
	register long x8 __asm__("x8") = syscall;
	register long x0 __asm__("x0");

	__asm__ volatile ("svc #0"
		: "=r" (x0)
		: "r" (x8) : __syscall_clobber);

	return x0;
}

static __always_inline long stub_syscall1(long syscall, long arg1)
{
	register long x8 __asm__("x8") = syscall;
	register long x0 __asm__("x0") = arg1;

	__asm__ volatile ("svc #0"
		: "+r" (x0)
		: "r" (x8) : __syscall_clobber);

	return x0;
}

static __always_inline long stub_syscall2(long syscall, long arg1, long arg2)
{
	register long x8 __asm__("x8") = syscall;
	register long x0 __asm__("x0") = arg1;
	register long x1 __asm__("x1") = arg2;

	__asm__ volatile ("svc #0"
		: "+r" (x0)
		: "r" (x8), "r" (x1) : __syscall_clobber);

	return x0;
}

static __always_inline long stub_syscall3(long syscall, long arg1, long arg2,
					  long arg3)
{
	register long x8 __asm__("x8") = syscall;
	register long x0 __asm__("x0") = arg1;
	register long x1 __asm__("x1") = arg2;
	register long x2 __asm__("x2") = arg3;

	__asm__ volatile ("svc #0"
		: "+r" (x0)
		: "r" (x8), "r" (x1), "r" (x2) : __syscall_clobber);

	return x0;
}

static __always_inline long stub_syscall4(long syscall, long arg1, long arg2,
					  long arg3, long arg4)
{
	register long x8 __asm__("x8") = syscall;
	register long x0 __asm__("x0") = arg1;
	register long x1 __asm__("x1") = arg2;
	register long x2 __asm__("x2") = arg3;
	register long x3 __asm__("x3") = arg4;

	__asm__ volatile ("svc #0"
		: "+r" (x0)
		: "r" (x8), "r" (x1), "r" (x2), "r" (x3)
		: __syscall_clobber);

	return x0;
}

static __always_inline long stub_syscall5(long syscall, long arg1, long arg2,
					  long arg3, long arg4, long arg5)
{
	register long x8 __asm__("x8") = syscall;
	register long x0 __asm__("x0") = arg1;
	register long x1 __asm__("x1") = arg2;
	register long x2 __asm__("x2") = arg3;
	register long x3 __asm__("x3") = arg4;
	register long x4 __asm__("x4") = arg5;

	__asm__ volatile ("svc #0"
		: "+r" (x0)
		: "r" (x8), "r" (x1), "r" (x2), "r" (x3), "r" (x4)
		: __syscall_clobber);

	return x0;
}

static __always_inline long stub_syscall6(long syscall, long arg1, long arg2,
					  long arg3, long arg4, long arg5,
					  long arg6)
{
	register long x8 __asm__("x8") = syscall;
	register long x0 __asm__("x0") = arg1;
	register long x1 __asm__("x1") = arg2;
	register long x2 __asm__("x2") = arg3;
	register long x3 __asm__("x3") = arg4;
	register long x4 __asm__("x4") = arg5;
	register long x5 __asm__("x5") = arg6;

	__asm__ volatile ("svc #0"
		: "+r" (x0)
		: "r" (x8), "r" (x1), "r" (x2), "r" (x3), "r" (x4), "r" (x5)
		: __syscall_clobber);

	return x0;
}

static __always_inline void trap_myself(void)
{
	__asm("brk #0");
}

/*
 * The stub data page sits directly above the stub code page; adrp
 * yields the current page, add one page size.
 */
static __always_inline void *get_stub_data(void)
{
	unsigned long ret;

	asm volatile (
		"adrp %0, . ;"
		"add %0, %0, %1 ;"
		: "=&r" (ret)
		: "r" (UM_KERN_PAGE_SIZE));

	return (void *)ret;
}

/*
 * Entry instructions of the stub_exe binary, emitted from file-scope
 * asm in stub_exe.c: move the stack pointer down past the future stub
 * mappings, then branch to real_init() (which never returns).
 */
#define STUB_EXE_START							\
	"	sub	sp, sp, #" __stringify(STUB_SIZE) "\n"		\
	"	b	real_init\n"

static __always_inline void
stub_seccomp_restore_state(struct stub_data_arch *arch)
{
	/* TPIDR_EL0 is writable at EL0, no syscall needed */
	if (arch->sync & STUB_SYNC_TLS)
		asm volatile ("msr TPIDR_EL0, %0" :: "r" (arch->tls));

	arch->sync = 0;
}

extern void stub_segv_handler(int, siginfo_t *, void *);
extern void stub_syscall_handler(void);
extern void stub_signal_interrupt(int, siginfo_t *, void *);
extern void stub_signal_restorer(void);

/*
 * arm64 has no SA_RESTORER: the host sets x30 to the VDSO's
 * __kernel_rt_sigreturn, and that svc does not execute from the stub
 * page: the seccomp filter TRAPs it, re-entering the handler until
 * the ~4.7KB frames overflow the stub sigstack. Enter the in-stub
 * restorer directly instead. ABI: the host enters the handler with
 * SP pointing at the rt_sigframe, whose first member is the siginfo,
 * so the handler's info argument is the frame address; restore SP
 * from it and branch. (Neither a C tail call (GCC emits bl, leaving
 * SP mid-frame) nor patching the saved LR (no frame pointer here)
 * works; this is layout-independent.)
 */
static __always_inline void stub_signal_return(void *frame)
{
	asm volatile("mov sp, %0\n"
		     "adr x9, stub_signal_restorer\n"
		     "br x9"
		     :: "r"(frame) : "x9", "memory");
	__builtin_unreachable();
}

/*
 * TPIDR_EL0 is EL0-writable and cannot be trapped, so unlike x86
 * (which forces TLS through arch_prctl) the kernel never observes
 * guest TLS writes. The trap handler runs with the guest's TPIDR
 * intact; save it so get_stub_state can learn it and fork/resume
 * can propagate it to new stubs.
 */
static __always_inline void stub_seccomp_save_state(struct stub_data_arch *arch)
{
	asm volatile ("mrs %0, TPIDR_EL0" : "=r" (arch->tls));
}

#endif
