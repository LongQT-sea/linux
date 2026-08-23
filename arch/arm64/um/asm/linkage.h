/* SPDX-License-Identifier: GPL-2.0 */
/*
 * asm/linkage.h for the arm64 UML backend, deliberately thin.
 *
 * UML assembles only host-agnostic stub .S files, which need just the
 * generic SYM_* macros (linux/linkage.h provides them).  Inheriting
 * native arm64's asm/linkage.h would drag in assembler.h and with it
 * the native cpufeature/memory.h chain, whose THREAD_SIZE collides
 * with UML's own asm/thread_info.h once two headers land in one
 * translation unit (found by allmodconfig + CONFIG_WERROR=y).
 */
#ifndef __UM_ARM64_LINKAGE_H
#define __UM_ARM64_LINKAGE_H

#include <asm-generic/linkage.h>

#endif
