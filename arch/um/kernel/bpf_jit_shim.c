// SPDX-License-Identifier: GPL-2.0
/*
 * Shims to reuse the x86-64 BPF JIT (arch/x86/net/bpf_jit_comp.c) under
 * User-Mode Linux.
 *
 * UML executes natively on the host, so the machine code the x86 JIT
 * emits runs as-is; what the JIT needs from the surrounding architecture
 * is provided here:
 *
 *  - text "pokes" are plain memcpy/memset. JIT/trampoline memory is
 *    writable at fill time and set read-only+executable once finalized;
 *    for finalized images, uml_text_poke_fixup() flips the range
 *    writable around the poke and restores it. smp_text_poke_single()
 *    is only reached on the fentry/tailcall path, which UML does not
 *    exercise yet; it is provided for link completeness.
 *  - cfi_mode: CFI is off under UML.
 *
 * The multi-byte NOP table (x86_nops) is shared from arch/x86/lib/nops.c
 * via subarch-y; clear_bhb_loop from arch/x86/entry/clearbhb.S.
 */
#include <linux/cache.h>
#include <linux/string.h>
#include <asm/cfi.h>
#include <asm/nops.h>
#include <asm/set_memory.h>
#include <asm/text-patching.h>	/* text_poke_set/smp_text_poke_single protos */

void *text_poke_set(void *addr, int c, size_t len)
{
	uml_text_poke_fixup((unsigned long)addr, len, true);
	memset(addr, c, len);
	uml_text_poke_fixup((unsigned long)addr, len, false);
	return addr;
}

void smp_text_poke_single(void *addr, const void *opcode, size_t len,
			  const void *emulate)
{
	uml_text_poke_fixup((unsigned long)addr, len, true);
	memcpy(addr, opcode, len);
	uml_text_poke_fixup((unsigned long)addr, len, false);
}

enum cfi_mode cfi_mode __ro_after_init = CFI_OFF;

/* see asm/percpu.h: address taken by the JIT, never dereferenced under UML */
unsigned long this_cpu_off;
