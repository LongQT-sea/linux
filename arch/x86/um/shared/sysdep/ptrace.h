/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SYSDEP_X86_PTRACE_H
#define __SYSDEP_X86_PTRACE_H

#include <generated/user_constants.h>
#include <sysdep/faultinfo.h>

#define MAX_REG_OFFSET (UM_FRAME_SIZE)
#define MAX_REG_NR ((MAX_REG_OFFSET) / sizeof(unsigned long))

/*
 * Full gp-frame size in unsigned longs. x86 has no UML-internal slots
 * past the ABI frame (user_regs_struct already carries ORIG_RAX);
 * shared-core gp[] buffers size off this, never off MAX_REG_NR.
 */
#define UM_GP_SLOTS (MAX_REG_NR)

#define REGS_IP(r) ((r)[HOST_IP])
#define REGS_SP(r) ((r)[HOST_SP])
#define REGS_EFLAGS(r) ((r)[HOST_EFLAGS])
#define REGS_AX(r) ((r)[HOST_AX])
#define REGS_BX(r) ((r)[HOST_BX])
#define REGS_CX(r) ((r)[HOST_CX])
#define REGS_DX(r) ((r)[HOST_DX])
#define REGS_SI(r) ((r)[HOST_SI])
#define REGS_DI(r) ((r)[HOST_DI])
#define REGS_BP(r) ((r)[HOST_BP])
#define REGS_CS(r) ((r)[HOST_CS])
#define REGS_SS(r) ((r)[HOST_SS])
#define REGS_DS(r) ((r)[HOST_DS])
#define REGS_ES(r) ((r)[HOST_ES])

#define UPT_IP(r) REGS_IP((r)->gp)
#define UPT_SP(r) REGS_SP((r)->gp)
#define UPT_EFLAGS(r) REGS_EFLAGS((r)->gp)
#define UPT_AX(r) REGS_AX((r)->gp)
#define UPT_BX(r) REGS_BX((r)->gp)
#define UPT_CX(r) REGS_CX((r)->gp)
#define UPT_DX(r) REGS_DX((r)->gp)
#define UPT_SI(r) REGS_SI((r)->gp)
#define UPT_DI(r) REGS_DI((r)->gp)
#define UPT_BP(r) REGS_BP((r)->gp)
#define UPT_CS(r) REGS_CS((r)->gp)
#define UPT_SS(r) REGS_SS((r)->gp)
#define UPT_DS(r) REGS_DS((r)->gp)
#define UPT_ES(r) REGS_ES((r)->gp)

#ifdef __i386__
#include "ptrace_32.h"
#else
#include "ptrace_64.h"
#endif

extern unsigned long host_fp_size;

struct uml_pt_regs {
	unsigned long gp[UM_GP_SLOTS];
	struct faultinfo faultinfo;
	long syscall;
	int is_user;

	/* Dynamically sized FP registers (holds an XSTATE) */
	unsigned long fp[];
};

#define EMPTY_UML_PT_REGS { }

#define UPT_SYSCALL_NR(r) ((r)->syscall)
#define UPT_FAULTINFO(r) (&(r)->faultinfo)
#define UPT_IS_USER(r) ((r)->is_user)

extern int arch_init_registers(int pid);

/* Initial stack pointer for a new thread (x86 tolerates 8 alignment) */
#define UM_THREAD_START_SP(stack) \
	((unsigned long)(stack) + UM_THREAD_SIZE - sizeof(void *))

#endif /* __SYSDEP_X86_PTRACE_H */
