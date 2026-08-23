/* SPDX-License-Identifier: GPL-2.0 */
/*
 * VMA default flags for the arm64 UML backend, mirroring the x86-64 UML
 * choice (arm64 stacks grow down).
 */

#ifndef __VM_FLAGS_ARM64_H
#define __VM_FLAGS_ARM64_H

#define VMA_STACK_DEFAULT_FLAGS append_vma_flags(VMA_DATA_FLAGS_EXEC, VMA_GROWSDOWN_BIT)

#endif
