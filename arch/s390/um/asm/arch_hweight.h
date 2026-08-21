/* SPDX-License-Identifier: GPL-2.0 */
/*
 * asm/arch_hweight.h for the s390x UML backend — the generic stub.
 *
 * Native s390's version includes asm/march.h (the z-series machine
 * level chain), which does not compile under UML where the CPU model
 * is the host itself and no march config exists.
 */
#ifndef _ASM_UM_S390_HWEIGHT_H
#define _ASM_UM_S390_HWEIGHT_H

#include <asm-generic/bitops/arch_hweight.h>

#endif
