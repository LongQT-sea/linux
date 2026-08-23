/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mmap/protection constants for the arm64 UML backend: the generic
 * asm-generic/mman.h with its default arch hooks (no BTI in the UML
 * guest model). Shadows native asm/mman.h (system_supports_bti chain).
 */
#ifndef __UM_ARM64_MMAN_H
#define __UM_ARM64_MMAN_H

#include <asm-generic/mman.h>

#endif
