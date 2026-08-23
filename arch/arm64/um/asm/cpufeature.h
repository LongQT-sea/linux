/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CPU capabilities for the arm64 UML backend: there are none to
 * discover.
 *
 * On hardware, cpus_have_final_cap() reports a capability bit
 * established at boot by reading ID registers at EL1. A UML guest runs
 * at EL0 as an ordinary process and cannot read them, and
 * arch/arm64/Kconfig, where every erratum symbol is defined, is not
 * sourced for ARCH=um, so none of those CONFIG symbols exist here
 * either.
 *
 * The reused arch/arm64/kernel/module-plts.c consults exactly one
 * capability, ARM64_WORKAROUND_843419 (a Cortex-A53 erratum affecting
 * ADRP placement), and consults it unconditionally rather than under
 * an #ifdef. Answering "no" is what the same code does on any part
 * without the erratum, and is the only answer this configuration can
 * give honestly; see the note in <asm/module.h>. The capability
 * numbers themselves come from the generated asm/cpucap-defs.h, which
 * this backend produces via arm64's kapi generation; only the query
 * needs a stub.
 */
#ifndef __UM_ARM64_CPUFEATURE_H
#define __UM_ARM64_CPUFEATURE_H

#include <linux/types.h>

static inline bool cpus_have_final_cap(int num)
{
	return false;
}

static inline bool system_supports_bti(void)
{
	return false;
}

#endif /* __UM_ARM64_CPUFEATURE_H */
