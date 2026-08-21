/* SPDX-License-Identifier: GPL-2.0 */
/*
 * String ABI for the s390x UML backend: no __HAVE_ARCH_* here — every
 * string function comes from generic lib/string.c. Shadowing the
 * native asm/string.h (which declares __HAVE_ARCH_MEMCPY and pulls
 * arch implementations) is deliberate: UML runs these natively on
 * the host CPU, and duplicating exports with lib/string.c breaks the
 * link. UML_SUBARCH_MEMCPY=y only suppresses user_syms.c's duplicate
 * EXPORT_SYMBOLs; it does not pull native objects (unlike x86-64).
 */
#ifndef __UM_S390_STRING_H
#define __UM_S390_STRING_H

#endif
