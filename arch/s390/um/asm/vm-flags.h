/* SPDX-License-Identifier: GPL-2.0 */
/*
 * VMA default flags for the s390x UML backend — mirrors the x86-64
 * UML choice (s390x stacks grow down).
 */

#ifndef __VM_FLAGS_S390_H
#define __VM_FLAGS_S390_H

#define VMA_STACK_DEFAULT_FLAGS append_vma_flags(VMA_DATA_FLAGS_EXEC, VMA_GROWSDOWN_BIT)

#endif
