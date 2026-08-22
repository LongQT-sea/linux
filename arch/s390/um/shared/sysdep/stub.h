/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The stub execution contract for the s390x UML backend:
 * `svc 0` with the syscall number in r1 (handles nr > 255; same shape
 * as native s390's GENERATE_SYSCALL_FUNC), trap-back via a 2-byte
 * ill-legal breakpoint (`j .+2` is wrong — use .insn or an illegal
 * halfword pair that SIGILLs), larl-based stub_data locator, stub
 * stack switch, and NO stub-carried TLS: acrs are ptrace-visible on
 * both consumption routes (F-s1), so save/restore state are no-ops.
 */
#ifndef __SYSDEP_STUB_H
#define __SYSDEP_STUB_H

#include <stddef.h>
#include <linux/stringify.h>
/* The stub TUs compile on the BUILD seat (x86_64) with host libc
 * headers, so <asm/unistd.h> would give the x86_64 table — wrong for
 * every number the s390x kernel dispatches on. Override ALL numbers
 * referenced by the stub/user TUs with their s390x values (uapi
 * asm/unistd_64.h). Any new syscall added to stub code MUST be
 * listed here. */
#undef __NR_exit
#undef __NR_exit_group
#define __NR_exit_group 248
#define __NR_exit 1
#undef __NR_read
#define __NR_read 3
#undef __NR_write
#define __NR_write 4
#undef __NR_close
#define __NR_close 6
#undef __NR_fcntl
#define __NR_fcntl 55
#undef __NR_sigaltstack
#define __NR_sigaltstack 186
#undef __NR_kill
#define __NR_kill 37
#undef __NR_rename
#define __NR_rename 38
#undef __NR_mkdir
#define __NR_mkdir 39
#undef __NR_rmdir
#define __NR_rmdir 40
#undef __NR_dup
#define __NR_dup 41
#undef __NR_pipe
#define __NR_pipe 42
#undef __NR_times
#define __NR_times 43
#undef __NR_getppid
#define __NR_getppid 64
#undef __NR_pread64
#define __NR_pread64 180
#undef __NR_futex
#define __NR_futex 238
#undef __NR_clock_nanosleep
#define __NR_clock_nanosleep 262
#undef __NR_prctl
#define __NR_prctl 172
#undef __NR_rt_sigaction
#define __NR_rt_sigaction 174
#undef __NR_rt_sigprocmask
#define __NR_rt_sigprocmask 175
#undef __NR_rt_sigreturn
#define __NR_rt_sigreturn 173
#undef __NR_seccomp
#define __NR_seccomp 348
#undef __NR_execveat
#define __NR_execveat 354
#undef __NR_mmap
#define __NR_mmap 90
#undef __NR_munmap
#define __NR_munmap 91
#undef __NR_recvmsg
#define __NR_recvmsg 372
#undef __NR_close_range
#define __NR_close_range 436
#undef __NR_ptrace
#define __NR_ptrace 26
#undef __NR_getpid
#define __NR_getpid 20
#include <sys/mman.h>
#include <as-layout.h>
#include <stub-data.h>
#include <sysdep/ptrace_user.h>
#include <generated/asm-offsets.h>
#include <linux/stddef.h>

#define STUB_MMAP_NR __NR_mmap

/*
 * s390 HAS SA_RESTORER (0x04000000): signal return goes through the
 * restorer trampoline in the stub page (svc __NR_rt_sigreturn passes
 * the filter's IP check) — x86 model, not arm64's vdso dance. [§5]
 */
#define UM_SA_RESTORER 0x04000000

/*
 * No in-stub TLS restore is needed: acrs[0..1] carry TLS and survive
 * traps intact (F-s1); the mcontext round-trip restores them. The
 * filter's TLS entry is vestigial, kept only for table shape.
 */
#define STUB_TLS_SYSCALL_NR (0x0f0000 + 5)

/*
 * s390 has ONLY old_mmap (__NR_mmap = 90 -> sys_old_mmap, one arg =
 * pointer to a struct mmap_arg_struct).
 */
struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

/*
 * Backend mmap invocation: stage the args in a struct and call the
 * single-arg syscall. [F-s3, P1]
 */
#define STUB_MMAP_CALL(res, addr, len, prot, flags, fd, off)		\
	do {								\
		struct mmap_arg_struct __margs = {			\
			(addr), (len), (prot), (flags),			\
			(unsigned long)(fd), (off),			\
		};							\
		(res) = stub_syscall1(STUB_MMAP_NR, (long)&__margs);	\
	} while (0)

#define MMAP_OFFSET(o) (o)

#define __syscall_clobber "memory"

static __always_inline long stub_syscall0(long syscall)
{
	register long r1 __asm__("1") = syscall;
	register long r2 __asm__("2");

	__asm__ volatile ("svc 0"
		: "=d" (r2)
		: "d" (r1)
		: __syscall_clobber);

	return r2;
}

#define STUB_SYSCALL(n)							\
static __always_inline long stub_syscall##n(long nr			\
		STUB_SYSCALL_PARM_##n)					\
{									\
	register long r1 __asm__("1") = nr;				\
	STUB_SYSCALL_REGS_##n;						\
	__asm__ volatile ("svc 0"					\
		: "+d" (r2)						\
		: "d" (r1) STUB_SYSCALL_INPUTS_##n			\
		: __syscall_clobber);					\
	return r2;							\
}

#define STUB_SYSCALL_INPUTS_1
#define STUB_SYSCALL_INPUTS_2 , "d" (r3)
#define STUB_SYSCALL_INPUTS_3 STUB_SYSCALL_INPUTS_2, "d" (r4)
#define STUB_SYSCALL_INPUTS_4 STUB_SYSCALL_INPUTS_3, "d" (r5)
#define STUB_SYSCALL_INPUTS_5 STUB_SYSCALL_INPUTS_4, "d" (r6)
#define STUB_SYSCALL_INPUTS_6 STUB_SYSCALL_INPUTS_5, "d" (r7)


#define STUB_SYSCALL_PARM_1 , long arg1
#define STUB_SYSCALL_PARM_2 STUB_SYSCALL_PARM_1, long arg2
#define STUB_SYSCALL_PARM_3 STUB_SYSCALL_PARM_2, long arg3
#define STUB_SYSCALL_PARM_4 STUB_SYSCALL_PARM_3, long arg4
#define STUB_SYSCALL_PARM_5 STUB_SYSCALL_PARM_4, long arg5
#define STUB_SYSCALL_PARM_6 STUB_SYSCALL_PARM_5, long arg6

#define STUB_SYSCALL_REGS_0 register long r2 __asm__("2") = arg1
#define STUB_SYSCALL_REGS_1 register long r2 __asm__("2") = arg1
#define STUB_SYSCALL_REGS_2 STUB_SYSCALL_REGS_1;			\
	register long r3 __asm__("3") = arg2
#define STUB_SYSCALL_REGS_3 STUB_SYSCALL_REGS_2;			\
	register long r4 __asm__("4") = arg3
#define STUB_SYSCALL_REGS_4 STUB_SYSCALL_REGS_3;			\
	register long r5 __asm__("5") = arg4
#define STUB_SYSCALL_REGS_5 STUB_SYSCALL_REGS_4;			\
	register long r6 __asm__("6") = arg5
#define STUB_SYSCALL_REGS_6 STUB_SYSCALL_REGS_5;			\
	register long r7 __asm__("7") = arg6

STUB_SYSCALL(1)
STUB_SYSCALL(2)
STUB_SYSCALL(3)
STUB_SYSCALL(4)
STUB_SYSCALL(5)
STUB_SYSCALL(6)

static __always_inline void trap_myself(void)
{
	/* s390 breakpoint opcode 0x0001: native illegal_op converts it
	 * to SIGTRAP/TRAP_BRKPT iff current->ptrace, else SIGILL
	 * (arch/s390/kernel/traps.c:151, uapi/asm/ptrace.h:307). One
	 * halfword; ILC=2. The kernel-side tracer reaps this stop after
	 * the futex handshake confirms the handler recorded its state. */
	__asm__ (".short 0x0001");
}

/*
 * The stub data region sits directly above the stub code page. larl
 * loads the address of a label INSIDE this function — NOT page-aligned
 * (the old +PAGE math silently produced a pointer 0x104..0xfff bytes
 * into the sigstack, desynchronizing the stub/kernel futex handshake).
 * Round the PC down to the page base first, then add one page.
 */
static __always_inline void *get_stub_data(void)
{
	unsigned long ret;

	asm volatile (
		"	larl	%0, 0f\n"
		"	nilf	%0, 0xfffff000\n"
		"0:	aghi	%0, %1\n"
		: "=&d" (ret)
		: "i" (UM_KERN_PAGE_SIZE)
		: "cc");

	return (void *)ret;
}

/*
 * Entry body of the stub_exe binary, expanded inside the shared
 * file-scope _start asm in stub_exe.c: move the stack pointer down
 * past the future stub mappings, then call real_init() (which never
 * returns; the idle jump after it is unreachable belt-and-braces).
 */
#define STUB_EXE_START							\
	"	aghi	%r15, -" __stringify(STUB_SIZE) "\n"		\
	"	brasl	%r14, real_init\n"				\
	"	j	.\n"

/*
 * Host rt_sigreturn takes the frame from %r15 (arch/s390/kernel/
 * signal.c sys_rt_sigreturn). The handler's siginfo pointer sits at
 * frame+168: 160-byte callee area + u16 svc_insn, padded to siginfo's
 * 8-byte alignment. Fixed ABI.
 */
static __always_inline void stub_signal_return(void *info)
{
	unsigned long frame = (unsigned long)info - 168;

	asm volatile (
		"	lgr	%%r15, %0\n"
		"	larl	%%r1, stub_signal_restorer\n"
		"	br	%%r1\n"
		:: "d" (frame) : "r1", "memory");
	__builtin_unreachable();
}

/* Nothing to save: everything visible to the kernel via regsets. */
static __always_inline void stub_seccomp_save_state(struct stub_data_arch *arch)
{
}

/*
 * Nothing to restore outside the mcontext: TLS/acrs ride the frame
 * (F-s1) — unlike x86 arch_prctl and arm64 TPIDR_EL0.
 */
static __always_inline void stub_seccomp_restore_state(struct stub_data_arch *arch)
{
}

/*
 * No s390x-specific stub init: no PAC keys (arm64), no TLS register
 * to set (F-s1 — acrs are ptrace-visible on both consumption routes).
 */
static __always_inline void stub_arch_init(unsigned long arch_flags)
{
}

#include <signal.h>

extern void stub_segv_handler(int, siginfo_t *, void *);
extern void stub_syscall_handler(void);
extern void stub_signal_interrupt(int, siginfo_t *, void *);
extern void stub_signal_restorer(void);

#endif
