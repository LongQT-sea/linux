/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cache geometry for the s390x UML backend — 256B lines, straight
 * from native arch/s390/include/asm/cache.h (census F-s3: verified in
 * the tree). ARCH_DMA_MINALIGN matches L1_CACHE_BYTES; UML does no
 * DMA (NO_DMA default).
 */
#ifndef __UM_S390_CACHE_H
#define __UM_S390_CACHE_H

#define L1_CACHE_SHIFT		8
#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)

#define ARCH_DMA_MINALIGN	(128)

#endif
