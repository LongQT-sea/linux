/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mcontext marshal interface for the s390x UML backend.
 * glibc mcontext_t is flat: { psw, gregs[16], aregs[16], fpregs } —
 * layout-identical to the kernel's _sigregs (F-s1, box-verified). No
 * record walk exists to get wrong.
 */
#ifndef __SYS_SIGCONTEXT_S390_H
#define __SYS_SIGCONTEXT_S390_H

#include <stub-data.h>
#include <sysdep/faultinfo.h>

extern void get_regs_from_mc(struct uml_pt_regs *, mcontext_t *);
extern void get_mc_from_regs(struct uml_pt_regs *regs, mcontext_t *mc,
			     int single_stepping);

extern int get_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
			  unsigned long *fp_size_out);
void get_stub_state_arg1(struct stub_data *data, unsigned long arg1);
extern int set_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
			  int single_stepping);

/*
 * GET_FAULTINFO_FROM_MC also executes inside the stub page
 * (stub_segv_handler), so it must stay call-closed: an out-of-section
 * call encodes a PC-relative displacement to the link address, which
 * replayed from the stub's runtime mapping lands nowhere. Keep it a
 * macro, as x86's form does.
 *
 * The s390 signal frame carries neither TEID nor FSI: si_addr (host-
 * computed from TEID) is all that reaches userland. In seccomp mode
 * the kernel-side caller re-fills addr from PTRACE_GETSIGINFO's
 * si_addr; here we classify from the program-interruption code in
 * si_errno/si_code where available and leave addr for the caller.
 */
#define GET_FAULTINFO_FROM_MC(fi, mc)					\
	do {								\
		(void)(mc);						\
		(fi).addr = 0;						\
		(fi).trap_no = 0;					\
		(fi).error_code = 0;					\
	} while (0)

#endif
