/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Register model for the arm64 UML backend.
 * arm64: X0..X30, SP, PC, PSTATE; syscall number in x8, args x0..x5,
 * result in x0.
 */
#ifndef __SYSDEP_ARM64_PTRACE_H
#define __SYSDEP_ARM64_PTRACE_H

#include <generated/user_constants.h>
#include <sysdep/faultinfo.h>

#define MAX_REG_OFFSET (UM_FRAME_SIZE)
#define MAX_REG_NR ((MAX_REG_OFFSET) / sizeof(unsigned long))

#define REGS_Xn(r, n) ((r)[HOST_X0 + (n)])
#define REGS_PC(r) ((r)[HOST_PC])
#define REGS_SP(r) ((r)[HOST_SP])
#define REGS_PSTATE(r) ((r)[HOST_PSTATE])

#define UPT_IP(r) REGS_PC((r)->gp)
#define UPT_SP(r) REGS_SP((r)->gp)
#define UPT_PSTATE(r) REGS_PSTATE((r)->gp)

#define UPT_SYSCALL_ARG1(r) ((r)->gp[HOST_X0])
#define UPT_SYSCALL_ARG2(r) ((r)->gp[HOST_X1])
#define UPT_SYSCALL_ARG3(r) ((r)->gp[HOST_X2])
#define UPT_SYSCALL_ARG4(r) ((r)->gp[HOST_X3])
#define UPT_SYSCALL_ARG5(r) ((r)->gp[HOST_X4])
#define UPT_SYSCALL_ARG6(r) ((r)->gp[HOST_X5])

extern unsigned long host_fp_size;

struct uml_pt_regs {
	unsigned long gp[MAX_REG_NR];
	struct faultinfo faultinfo;
	long syscall;
	int is_user;

	/* Dynamically sized FP registers (fpsimd/SVE) */
	unsigned long fp[];
};

#define EMPTY_UML_PT_REGS { }

#define UPT_SYSCALL_NR(r) ((r)->syscall)
#define UPT_FAULTINFO(r) (&(r)->faultinfo)
#define UPT_IS_USER(r) ((r)->is_user)

extern int arch_init_registers(int pid);

#endif /* __SYSDEP_ARM64_PTRACE_H */
