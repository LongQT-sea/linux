// SPDX-License-Identifier: GPL-2.0
/*
 * user-offsets.c — the root parameter file of the s390x UML backend.
 * Emits HOST_* register indices (in longs) for the NT_PRSTATUS payload
 * `s390_regs` { psw(2 longs), gprs[16], acrs[16] (32-bit!), orig_gpr2 }
 * — box-verified 216 bytes (F-s1) — plus frame size and poll/prot
 * constants.
 *
 * Endianness-and-width trap (DESIGN §7.1): acrs[] are 32-bit halves
 * inside a long-indexed frame. On big-endian, acrs[0] occupies the LOW
 * half of long slot 9; HOST_ACRS(i) therefore names the containing
 * long and consumers must shift by (i & 1 ? 0 : 32). user-offsets
 * emits both a byte offset (HOST_ACRS_OFF) and the long index of the
 * first acr so nothing here assumes little-endian layout.
 */
#include <stdio.h>
#include <stddef.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <linux/ptrace.h>
#include <asm/types.h>
#include <asm/posix_types.h>
#include <linux/kbuild.h>

/* workaround for a warning with -Wmissing-prototypes */
void foo(void);

typedef struct {
	unsigned long mask;
	unsigned long addr;
} __attribute__((aligned(8))) um_psw_t;

/* The NT_PRSTATUS regset payload: kernel's s390_regs (uapi/asm/ptrace.h).
 * Defined locally rather than #include'd from the native tree to keep
 * this file compiling against plain host + uapi headers only. */
struct um_s390_regs {
	um_psw_t psw;
	unsigned long gprs[16];
	unsigned int acrs[16];
	unsigned long orig_gpr2;
};

#define OFF(sym, field)	\
	DEFINE(sym, offsetof(struct um_s390_regs, field) / \
		     sizeof(unsigned long))

#define OFFB(sym, field)	\
	DEFINE(sym, offsetof(struct um_s390_regs, field))

void foo(void)
{
	OFF(HOST_PSW_MASK, psw.mask);
	OFF(HOST_PSW_ADDR, psw.addr);
	OFFB(HOST_PSW_MASK_OFF, psw.mask);
	OFFB(HOST_PSW_ADDR_OFF, psw.addr);

	OFF(HOST_GPR0, gprs[0]);
	OFF(HOST_SP, gprs[15]);		/* r15 = stack pointer */
	OFF(HOST_ORIG_GPR2, orig_gpr2);	/* preserved entry arg1 */

	/* Access registers: 32-bit slots in a long-indexed frame.
	 * HOST_ACRS = index of the long holding acrs[0];
	 * HOST_ACRS_OFF = byte offset of acrs[0] within the frame. */
	DEFINE(HOST_ACRS, offsetof(struct um_s390_regs, acrs[0]) /
			  sizeof(unsigned long));
	OFFB(HOST_ACRS_OFF, acrs[0]);

	DEFINE(UM_FRAME_SIZE, sizeof(struct um_s390_regs));

	/*
	 * s390 rt_sigframe minimum: 160 callee area + svc insn word +
	 * siginfo + ucontext{flags,link,stack,_sigregs(344B incl.
	 * fpregs 136),sigmask} ~= 700 bytes — fits one page easily,
	 * but keep two like arm64 for headroom (altstack vectors etc).
	 */
	DEFINE(UM_STUB_SIGSTACK_PAGES, 1);

	DEFINE(UM_POLLIN, POLLIN);
	DEFINE(UM_POLLPRI, POLLPRI);
	DEFINE(UM_POLLOUT, POLLOUT);

	DEFINE(UM_PROT_READ, PROT_READ);
	DEFINE(UM_PROT_WRITE, PROT_WRITE);
	DEFINE(UM_PROT_EXEC, PROT_EXEC);
}
