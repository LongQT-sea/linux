// SPDX-License-Identifier: GPL-2.0
/*
 * user-offsets.c: the root parameter file of the arm64 UML backend.
 * Emits HOST_* register indices (in longs) for arm64's
 * struct user_regs_struct { regs[31], sp, pc, pstate }, plus the frame
 * size and the poll/prot constants UML consumes.
 */
#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <linux/ptrace.h>
#include <asm/types.h>
#include <linux/kbuild.h>

/* workaround for a warning with -Wmissing-prototypes */
void foo(void);

#define OFF(sym, field)	\
	DEFINE(sym, offsetof(struct user_regs_struct, field) / \
		     sizeof(unsigned long))

void foo(void)
{
	OFF(HOST_X0, regs[0]);
	OFF(HOST_X1, regs[1]);
	OFF(HOST_X2, regs[2]);
	OFF(HOST_X3, regs[3]);
	OFF(HOST_X4, regs[4]);
	OFF(HOST_X5, regs[5]);
	OFF(HOST_X6, regs[6]);
	OFF(HOST_X7, regs[7]);
	OFF(HOST_X8, regs[8]);
	OFF(HOST_X9, regs[9]);
	OFF(HOST_X10, regs[10]);
	OFF(HOST_X11, regs[11]);
	OFF(HOST_X12, regs[12]);
	OFF(HOST_X13, regs[13]);
	OFF(HOST_X14, regs[14]);
	OFF(HOST_X15, regs[15]);
	OFF(HOST_X16, regs[16]);
	OFF(HOST_X17, regs[17]);
	OFF(HOST_X18, regs[18]);
	OFF(HOST_X19, regs[19]);
	OFF(HOST_X20, regs[20]);
	OFF(HOST_X21, regs[21]);
	OFF(HOST_X22, regs[22]);
	OFF(HOST_X23, regs[23]);
	OFF(HOST_X24, regs[24]);
	OFF(HOST_X25, regs[25]);
	OFF(HOST_X26, regs[26]);
	OFF(HOST_X27, regs[27]);
	OFF(HOST_X28, regs[28]);
	OFF(HOST_X29, regs[29]);
	OFF(HOST_X30, regs[30]);
	OFF(HOST_SP, sp);
	OFF(HOST_PC, pc);
	OFF(HOST_PSTATE, pstate);

	DEFINE(UM_FRAME_SIZE, sizeof(struct user_regs_struct));
	DEFINE(UM_POLLIN, POLLIN);
	DEFINE(UM_POLLPRI, POLLPRI);
	DEFINE(UM_POLLOUT, POLLOUT);

	DEFINE(UM_PROT_READ, PROT_READ);
	DEFINE(UM_PROT_WRITE, PROT_WRITE);
	DEFINE(UM_PROT_EXEC, PROT_EXEC);
}
