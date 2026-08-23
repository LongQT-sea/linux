/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Signal ABI for the arm64 UML backend: the generic uapi definitions
 * plus the identity untagged-address helper (UML guests do not use
 * arm64 top-byte-ignore). Shadows the native asm/signal.h, which
 * pulls native memory.h (CONFIG_ARM64_VA_BITS class).
 */
#ifndef __UM_ARM64_SIGNAL_H
#define __UM_ARM64_SIGNAL_H

#include <uapi/asm/signal.h>
#include <uapi/asm/siginfo.h>

static inline void __user *arch_untagged_si_addr(void __user *addr,
						 unsigned long sig,
						 unsigned long si_code)
{
	return addr;
}
#define arch_untagged_si_addr arch_untagged_si_addr

#endif
