/* SPDX-License-Identifier: GPL-2.0 */
/*
 * String ABI for the arm64 UML backend: deliberately no __HAVE_ARCH_*
 * for now; every string function comes from generic
 * lib/string.c (which also yields kernel_strrchr via the arch/um
 * -Dstrrchr remap). The native arch/arm64/lib objects can return as a
 * deliberate optimization later (they run natively, but duplicate the
 * generic exports when both are linked).
 */
#ifndef __UM_ARM64_STRING_H
#define __UM_ARM64_STRING_H

#endif
