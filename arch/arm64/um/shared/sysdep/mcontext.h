/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mcontext marshal interface for the arm64 UML backend.
 * arm64's mcontext_t carries regs[31]/sp/pc/pstate + fault_address;
 * the ESR lives in the sigcontext extra-record chain (ESR_MAGIC).
 */
#ifndef __SYS_SIGCONTEXT_ARM64_H
#define __SYS_SIGCONTEXT_ARM64_H

#include <stub-data.h>
#include <sysdep/faultinfo.h>

extern void get_regs_from_mc(struct uml_pt_regs *, mcontext_t *);
extern void get_mc_from_regs(struct uml_pt_regs *regs, mcontext_t *mc,
			     int single_stepping);

extern int get_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
			  unsigned long *fp_size_out);
extern int set_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
			  int single_stepping);

/*
 * GET_FAULTINFO_FROM_MC also executes inside the stub page
 * (stub_segv_handler), so it must stay call-closed: an out-of-section
 * call encodes the PC-relative displacement to the link address, which
 * replayed from the stub's runtime mapping lands at
 * stub_base + displacement, i.e. nowhere. Keep it inline, as x86's
 * macro form does.
 */
#define ESR_MAGIC 0x45535201

static __always_inline void get_faultinfo_from_mc(struct faultinfo *fi,
						  mcontext_t *mc)
{
	struct { unsigned int magic; unsigned int size; } *h;
	unsigned long long esr = 0;

	fi->addr = mc->fault_address;
	for (h = (void *)mc->__reserved; h->magic; h = (void *)h + h->size) {
		if (h->magic == ESR_MAGIC) {
			esr = *(unsigned long long *)(h + 1);
			break;
		}
	}
	fi->error_code = esr;
	fi->trap_no = (esr >> 26) & 0x3f;
}

#define GET_FAULTINFO_FROM_MC(fi, mc) get_faultinfo_from_mc(&(fi), mc)

#endif
