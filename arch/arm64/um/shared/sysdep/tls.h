/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SYSDEP_TLS_H
#define _SYSDEP_TLS_H

/*
 * No GDT/LDT on arm64: thread-local storage is the TPIDR_EL0
 * register. The typedef exists only so the shared stub-data.h include
 * resolves; nothing here is used on this backend.
 */
typedef struct um_dup_user_desc {
	unsigned int __unused;
} user_desc_t;

#endif /* _SYSDEP_TLS_H */
