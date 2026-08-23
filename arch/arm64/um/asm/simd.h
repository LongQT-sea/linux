/* SPDX-License-Identifier: GPL-2.0 */
/*
 * asm/simd.h for the arm64 UML backend: the generic stub.
 *
 * UML has no kernel NEON/SVE context (guest SIMD state lives in the
 * userspace register frame), so native arm64's simd.h, which drags in
 * neon.h/fpsimd.h and the native user_fpsimd_state, cannot compile
 * here.  Generic crypto code only needs may_use_simd().
 */
#ifndef __UM_ARM64_SIMD_H
#define __UM_ARM64_SIMD_H

#include <asm-generic/simd.h>

#endif
