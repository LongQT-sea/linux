/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_SIGCONTEXT_WRAPPER_H
#define _ASM_S390_SIGCONTEXT_WRAPPER_H

/*
 * Legacy wrapper — kernel code should include <uapi/asm/sigcontext.h>
 * directly. Kept so native s390 headers (fpu-types.h et al.) resolve
 * when reused by the UML s390x backend. Distinct include guard from
 * the uapi header's _ASM_S390_SIGCONTEXT_H on purpose.
 */

#include <uapi/asm/sigcontext.h>

#endif /* _ASM_S390_SIGCONTEXT_WRAPPER_H */
