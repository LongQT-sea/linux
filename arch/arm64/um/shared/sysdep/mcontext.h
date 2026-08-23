/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mcontext marshal interface for the arm64 UML backend.
 * arm64's mcontext_t carries regs[31]/sp/pc/pstate + fault_address;
 * the ESR lives in the sigcontext extra-record chain (ESR_MAGIC),
 * walked by the backend's mcontext.c.
 */
#ifndef __SYS_SIGCONTEXT_ARM64_H
#define __SYS_SIGCONTEXT_ARM64_H

#include <stub-data.h>

extern void get_regs_from_mc(struct uml_pt_regs *, mcontext_t *);
extern void get_mc_from_regs(struct uml_pt_regs *regs, mcontext_t *mc,
			     int single_stepping);

extern int get_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
			  unsigned long *fp_size_out);
extern int set_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
			  int single_stepping);

extern void get_faultinfo_from_mc(struct faultinfo *fi, mcontext_t *mc);

#define GET_FAULTINFO_FROM_MC(fi, mc) get_faultinfo_from_mc(&(fi), mc)

#endif
