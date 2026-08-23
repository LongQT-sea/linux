/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_SEGMENT_H
#define __UM_SEGMENT_H

extern int host_gdt_entry_tls_min;

#define GDT_ENTRY_TLS_ENTRIES 3
#define GDT_ENTRY_TLS_MIN host_gdt_entry_tls_min
#define GDT_ENTRY_TLS_MAX (GDT_ENTRY_TLS_MIN + GDT_ENTRY_TLS_ENTRIES - 1)

/*
 * UML has no GDT; this exists only to satisfy shared x86 header code
 * (e.g. the verw inline in nospec-branch.h) that UML never executes.
 */
#define __KERNEL_DS 0

#endif
