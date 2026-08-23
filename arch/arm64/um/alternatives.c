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
