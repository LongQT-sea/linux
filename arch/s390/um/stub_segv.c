// SPDX-License-Identifier: GPL-2.0
/*
 * stub_segv_handler for the s390x UML backend — copy of the
 * arm64/x86 shape once faultinfo.h/mcontext.h exist: record the
 * faultinfo into the stub_data page, then trap back to the tracer.
 */

#include <sysdep/stub.h>
#include <sysdep/faultinfo.h>
#include <sysdep/mcontext.h>
#include <sys/ucontext.h>

void __attribute__ ((__section__ (".__syscall_stub")))
stub_segv_handler(int sig, siginfo_t *info, void *p)
{
	struct faultinfo *f = get_stub_data();
	ucontext_t *uc = p;

	GET_FAULTINFO_FROM_MC(*f, &uc->uc_mcontext);
	trap_myself();
}
