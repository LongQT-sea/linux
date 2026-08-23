// SPDX-License-Identifier: GPL-2.0
/*
 * arm64 alternatives callback stub for UML: the backend does not
 * apply alternatives (there is no live text patching under UML).
 */
#include <linux/module.h>
#include <linux/types.h>

struct alt_instr;

void alt_cb_patch_nops(struct alt_instr *alt, __le32 *origptr,
		       __le32 *updptr, int nr_inst)
{
}
EXPORT_SYMBOL(alt_cb_patch_nops);

/*
 * Called by the reused arch/arm64/kernel/module.c on any
 * .altinstructions section a module carries. No code built for this
 * configuration emits ALTERNATIVE sequences (arch/arm64/Kconfig, where
 * every capability and erratum symbol lives, is not sourced for
 * ARCH=um), so a UML module has no such section and this never runs;
 * it exists so the shared loader links.
 */
int apply_alternatives_module(void *start, size_t length)
{
	return 0;
}
