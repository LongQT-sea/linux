/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __UM_X86_KPROBES_H
#define __UM_X86_KPROBES_H

/*
 * asm/kprobes.h for UML/x86: the x86 instruction-level type definitions,
 * minus the native-only pieces (the current_top_of_stack()-based stack
 * sizing is unused here). The int3_emulate_*() helpers are shared with
 * native x86 via asm/text-patching.h.
 */

#include <asm-generic/kprobes.h>

#ifdef CONFIG_KPROBES
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>
#include <asm/insn.h>

#define  __ARCH_WANT_KPROBES_INSN_SLOT

struct pt_regs;
struct kprobe;

typedef u8 kprobe_opcode_t;

#define flush_insn_slot(p)	do { } while (0)

extern const int kretprobe_blacklist_size;

void arch_remove_kprobe(struct kprobe *p);

/* Architecture specific copy of original instruction */
struct arch_specific_insn {
	/* copy of the original instruction */
	kprobe_opcode_t *insn;
	/*
	 * boostable = 0: This instruction type is not boostable.
	 * boostable = 1: This instruction has been boosted: we have
	 * added a relative jump after the instruction copy in insn,
	 * so no single-step and fixup are needed (unless there's
	 * a post_handler).
	 */
	unsigned boostable:1;
	unsigned char size;	/* The size of insn */
	union {
		unsigned char opcode;
		struct {
			unsigned char type;
		} jcc;
		struct {
			unsigned char type;
			unsigned char asize;
		} loop;
		struct {
			unsigned char reg;
		} indirect;
	};
	s32 rel32;	/* relative offset must be s32, s16, or s8 */
	void (*emulate_op)(struct kprobe *p, struct pt_regs *regs);
	/* Number of bytes of text poked */
	int tp_len;
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned long status;
	unsigned long old_flags;
	unsigned long saved_flags;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned long kprobe_status;
	unsigned long kprobe_old_flags;
	unsigned long kprobe_saved_flags;
	struct prev_kprobe prev_kprobe;
};

extern int kprobe_fault_handler(struct pt_regs *regs, int trapnr);
extern int kprobe_int3_handler(struct pt_regs *regs);

#endif /* CONFIG_KPROBES */
#endif /* __UM_X86_KPROBES_H */
