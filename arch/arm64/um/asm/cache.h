/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cache geometry for the arm64 UML backend: 64B shift (the native
 * arm64 default); ARCH_DMA_MINALIGN 128 matches Apple silicon's line.
 * Deliberately no arch_sync_dma_flush here: dma-map-ops.h provides it
 * under its config guard.
 */
#ifndef __UM_ARM64_CACHE_H
#define __UM_ARM64_CACHE_H

#define L1_CACHE_SHIFT		6
#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)

#define ARCH_DMA_MINALIGN	(128)

#endif
