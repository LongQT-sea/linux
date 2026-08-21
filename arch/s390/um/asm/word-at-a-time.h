/* SPDX-License-Identifier: GPL-2.0 */
/*
 * word-at-a-time for the s390x UML backend — the generic version.
 * The native s390 header is fine in isolation, but shadowing it keeps
 * the backend's asm surface explicit and endianness-audited (§7):
 * the generic big-endian path is what we want to be testing.
 */
#ifndef __UM_S390_WORD_AT_A_TIME_H
#define __UM_S390_WORD_AT_A_TIME_H

#include <asm-generic/word-at-a-time.h>

#endif
