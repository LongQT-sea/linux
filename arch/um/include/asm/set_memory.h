/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UM_SET_MEMORY_H
#define _ASM_UM_SET_MEMORY_H

/*
 * Real set_memory_* for UML via host mprotect (see
 * arch/um/kernel/set_memory.c). This header shadows the host-arch (x86)
 * declarations so <asm/set_memory.h> resolves here.
 */
#include <linux/types.h>

int set_memory_rox(unsigned long addr, int numpages);
#define set_memory_rox set_memory_rox
int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_x(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);

/* text_poke writable-window helper (ROX'd JIT/trampoline ranges only) */
void uml_text_poke_fixup(unsigned long addr, size_t len, bool writable);

/* write to (host r-x) kernel text, e.g. the BPF fentry patch sites */
void uml_kernel_text_poke(void *addr, const void *opcode, size_t len);

#endif /* _ASM_UM_SET_MEMORY_H */
