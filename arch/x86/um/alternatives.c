// SPDX-License-Identifier: GPL-2.0
/*
 * x86 alternatives / text-patching API stubs for UML, moved verbatim
 * out of arch/um/kernel/um_arch.c. UML does not patch live text on x86;
 * these exist because the build reuses x86 kernel code that references
 * the interface.
 */
#include <linux/mm.h>
#include <linux/string.h>
#include <asm/alternative.h>
#include <asm/text-patching.h>

void apply_seal_endbr(s32 *start, s32 *end)
{
}

void apply_retpolines(s32 *start, s32 *end)
{
}

void apply_returns(s32 *start, s32 *end)
{
}

void apply_fineibt(s32 *start_retpoline, s32 *end_retpoline,
		   s32 *start_cfi, s32 *end_cfi)
{
}

void apply_alternatives(struct alt_instr *start, struct alt_instr *end)
{
}

void *text_poke(void *addr, const void *opcode, size_t len)
{
	/*
	 * In UML, the only reference to this function is in
	 * apply_relocate_add(), which shouldn't ever actually call this
	 * because UML doesn't have live patching.
	 */
	WARN_ON(1);

	return memcpy(addr, opcode, len);
}

void *text_poke_copy(void *addr, const void *opcode, size_t len)
{
	return text_poke(addr, opcode, len);
}

void smp_text_poke_sync_each_cpu(void)
{
}
