/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Empty placeholder: segments are an x86 concept; arch/um's
 * thread_info.h includes this header but uses nothing from it.
 * (Same role as the x86 backend's empty apic.h/irq_vectors.h.)
 */
#ifndef __UM_ARM64_SEGMENT_H
#define __UM_ARM64_SEGMENT_H

/* No segments, no GDT TLS on arm64; the stub TLS array is empty. */
#define UM_TLS_ENTRIES 0

#endif
