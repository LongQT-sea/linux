// SPDX-License-Identifier: GPL-2.0
#define __NO_FORTIFY
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kconfig.h>

/*
 * This file exports some critical string functions and compiler
 * built-in functions (where calls are emitted by the compiler
 * itself that we cannot avoid even in kernel code) to modules.
 *
 * "_user.c" code that previously used exports here such as hostfs
 * really should be considered part of the 'hypervisor' and define
 * its own API boundary like hostfs does now; don't add exports to
 * this file for such cases.
 */

/* If it's not defined, the export is included in lib/string.c.*/
#ifdef __HAVE_ARCH_STRSTR
#undef strstr
EXPORT_SYMBOL(strstr);
#endif

/*
 * Export these string functions only when the backend does not provide
 * its own memcpy and selects UML_SUBARCH_MEMCPY: the x86-64 backend
 * pulls in arch/x86/lib/memcpy_64.o, while i386 still needs the
 * exports.
 */
#if !IS_ENABLED(CONFIG_UML_SUBARCH_MEMCPY)
#undef memcpy
extern void *memcpy(void *, const void *, size_t);
EXPORT_SYMBOL(memcpy);
extern void *memmove(void *, const void *, size_t);
EXPORT_SYMBOL(memmove);
#undef memset
extern void *memset(void *, int, size_t);
EXPORT_SYMBOL(memset);
#endif

#ifdef _FORTIFY_SOURCE
extern int __sprintf_chk(char *str, int flag, size_t len, const char *format, ...);
EXPORT_SYMBOL(__sprintf_chk);
#endif
