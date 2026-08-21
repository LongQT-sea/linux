/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KASAN layout for the s390x UML backend — mirrors the x86 UM
 * kasan.h with the s390 host user VA bound. s390x user space ends at
 * 2^63 - 4096 (REGION2-size address space is a native-kernel concept;
 * userspace VA is 64-bit with 8EB max, practically the kernel test
 * range); use the conservative 62-bit user-space end that matches
 * observed host mappings (libc at ~0x3ff_xxxxxxxx, F-s1) and leaves
 * room below.
 */
#ifndef __ASM_UM_S390_KASAN_H
#define __ASM_UM_S390_KASAN_H

#include <linux/init.h>
#include <linux/const.h>

#define KASAN_SHADOW_OFFSET _AC(CONFIG_KASAN_SHADOW_OFFSET, UL)

/* used in kasan_mem_to_shadow to divide by 8 */
#define KASAN_SHADOW_SCALE_SHIFT 3

#define KASAN_HOST_USER_SPACE_END_ADDR 0x3fffffffffffffffUL
/* KASAN_SHADOW_SIZE is the size of total address space divided by 8 */
#define KASAN_SHADOW_SIZE ((KASAN_HOST_USER_SPACE_END_ADDR + 1) >> \
			KASAN_SHADOW_SCALE_SHIFT)

#define KASAN_SHADOW_START (KASAN_SHADOW_OFFSET)
#define KASAN_SHADOW_END (KASAN_SHADOW_START + KASAN_SHADOW_SIZE)

#ifdef CONFIG_KASAN
void kasan_init(void);
#else
static inline void kasan_init(void) { }
#endif /* CONFIG_KASAN */

#endif
