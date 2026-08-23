/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UM_KDEBUG_H
#define _ASM_UM_KDEBUG_H

/*
 * UML gets DIE_UNUSED/DIE_OOPS from the generic header contract; DIE_INT3
 * and DIE_DEBUG exist so the subarch-reused x86 uprobes code (which
 * consumes them on real hardware) compiles. UML does not raise die
 * notifications for user traps: the guest-user int3/single-step relay in
 * arch/um/kernel/trap.c calls the uprobe core notifiers directly (the
 * arm64/riscv pattern), so these values are currently compile-time only.
 */
enum die_val {
	DIE_UNUSED,
	DIE_OOPS = 1,
	DIE_INT3,
	DIE_DEBUG,
};

#endif /* _ASM_UM_KDEBUG_H */
