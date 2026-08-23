// SPDX-License-Identifier: GPL-2.0
/*
 * Multi-byte NOP table, extracted from arch/x86/kernel/alternative.c
 * so that both native x86 and UML (which does not build alternative.c)
 * share the canonical definition.
 */
#include <linux/types.h>
#include <asm/nops.h>

static const unsigned char x86nops[] = {
	BYTES_NOP1,
	BYTES_NOP2,
	BYTES_NOP3,
	BYTES_NOP4,
	BYTES_NOP5,
	BYTES_NOP6,
	BYTES_NOP7,
	BYTES_NOP8,
#ifdef CONFIG_64BIT
	BYTES_NOP9,
	BYTES_NOP10,
	BYTES_NOP11,
#endif
};

const unsigned char * const x86_nops[ASM_NOP_MAX+1] = {
	NULL,
	x86nops,
	x86nops + 1,
	x86nops + 1 + 2,
	x86nops + 1 + 2 + 3,
	x86nops + 1 + 2 + 3 + 4,
	x86nops + 1 + 2 + 3 + 4 + 5,
	x86nops + 1 + 2 + 3 + 4 + 5 + 6,
	x86nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
#ifdef CONFIG_64BIT
	x86nops + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8,
	x86nops + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9,
	x86nops + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10,
#endif
};
