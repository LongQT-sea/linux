/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Register/ptrace constants for the arm64 UML backend.
 * arm64 ABI: syscall number in x8 at the boundary; the regset marshal
 * mirrors it into the dead HOST_SYSCALLNO slot, which is what
 * PT_SYSCALL_NR names (writes to it, e.g. the -ERESTARTSYS guard's
 * -1, must never reach a live GPR). Result in x0, pc/sp/pstate as named.
 */
#ifndef __SYSDEP_ARM64_PTRACE_USER_H
#define __SYSDEP_ARM64_PTRACE_USER_H

#include <generated/user_constants.h>

#define PT_OFFSET(r) ((r) * sizeof(long))

#define PT_SYSCALL_NR(regs) ((regs)[HOST_SYSCALLNO])
#define PT_SYSCALL_NR_OFFSET PT_OFFSET(HOST_SYSCALLNO)
#define PT_SYSCALL_RET_OFFSET PT_OFFSET(HOST_X0)

#define REGS_IP_INDEX HOST_PC
#define REGS_SP_INDEX HOST_SP

/*
 * arm64 makes one general-purpose register unusable at a ptrace syscall
 * stop.
 *
 * To tell a tracer whether a stop came from syscall entry or syscall
 * exit, the arm64 kernel overwrites a register in the tracee with the
 * direction (x7 for AArch64, r12 for AArch32) and puts the tracee's own
 * value back when the stop ends. ptrace_save_reg() in
 * arch/arm64/kernel/ptrace.c spells out the consequences: reads by the
 * tracer see the direction rather than the register, and writes by the
 * tracer during the stop are discarded.
 *
 * x86 has no equivalent, so nothing in UML expected it, and the result
 * is guest-visible data corruption rather than a missing feature: UML
 * reads a thread's registers at the stop, keeps them, and writes them
 * back later, possibly into a stub process another thread of the same
 * guest mm ran on last. Both halves break: the read stores the stop
 * direction (0) into UML's copy of the thread's x7, and the write is
 * silently dropped, so the stub keeps whichever thread's x7 ran last.
 * A value a threaded guest holds in x7 across a loop turns into zero
 * or another thread's value at an arbitrary point.
 *
 * The escape: resuming the stop with PTRACE_SINGLESTEP lands on a
 * pseudo-step SIGTRAP where the register is readable and writable,
 * without executing a guest instruction. See userspace() in
 * arch/um/os-Linux/skas/process.c.
 *
 * SECCOMP mode is unaffected: its traps are ordinary signals, and the
 * arm64 comment quoted above notes that seccomp and pseudo-step traps
 * nobble nothing.
 */
#define UM_SYSCALL_STOP_HIDES_REG 1
#define UM_SYSCALL_STOP_HIDDEN_REG HOST_X7

/*
 * fpsimd state, two layouts.
 *
 * The canonical in-kernel layout of regs->fp is user_fpsimd_state:
 * {vregs[32], fpsr, fpcr, __reserved[2]}, 528 bytes. It is what the
 * host NT_PRFPREG regset transfers in ptrace mode and what the guest
 * NT_PRFPREG regset and core dumps expose, in both modes.
 *
 * A sigcontext fpsimd record instead carries {header(8), fpsr, fpcr,
 * vregs[32]}: 528 bytes total, 520 of payload after the 8-byte
 * header. The seccomp-mode mcontext marshal and the guest sigframe
 * code convert between the two at their boundaries.
 *
 * The os-Linux side compiles against libc headers that do not carry
 * the kernel's struct names, so both layouts are spelled out here.
 */
struct um_fpsimd_state {
	__uint128_t vregs[32];
	unsigned int fpsr;
	unsigned int fpcr;
	unsigned int __reserved[2];
};

struct um_fpsimd_payload {
	unsigned int fpsr;
	unsigned int fpcr;
	__uint128_t vregs[32];
};

#define UM_FPSIMD_STATE_SIZE sizeof(struct um_fpsimd_state)
#define UM_FPSIMD_PAYLOAD_SIZE sizeof(struct um_fpsimd_payload)

/*
 * glibc may lag the kernel uapi for SYSEMU on arm64 (SYSEMU exists
 * there since Linux 5.3); provide the numbers when missing.
 */
#ifndef PTRACE_SYSEMU
#define PTRACE_SYSEMU 31
#endif
#ifndef PTRACE_SYSEMU_SINGLESTEP
#define PTRACE_SYSEMU_SINGLESTEP 32
#endif

#endif /* __SYSDEP_ARM64_PTRACE_USER_H */
