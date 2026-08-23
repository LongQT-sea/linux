// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rethook trampoline for UML/x86.
 *
 * The kretprobe return path under um runs entirely in-process: at
 * entry-probe time arch_rethook_prepare() plants the trampoline over
 * the caller's return address on the guest kernel stack; the probed
 * function's `ret` then jumps here. No signals are involved on the
 * return path (int3 is only needed at entry).
 *
 * Differences from the native x86 trampoline (arch/x86/kernel/rethook.c):
 * the register dump feeds a full, zeroed um pt_regs in C (um's pt_regs
 * wraps uml_pt_regs: gp[] in HOST_* order, which is the native order,
 * plus tail fields a raw asm frame cannot fake); resume is a plain ret
 * through the stack slot after the handler leaves the real target in
 * PT_REGS_IP. eflags is not captured/restored: under um the guest
 * eflags are virtual state (IRQ control is signal masks) and the
 * tracing consumers don't read them.
 */
#include <linux/rethook.h>
#include <linux/kprobes.h>
#include <linux/linkage.h>
#include <asm/ptrace.h>

__visible void arch_rethook_trampoline_callback(unsigned long *dump,
						unsigned long *frame);

asm(
	".text\n"
	".globl arch_rethook_trampoline\n"
	".type arch_rethook_trampoline, @function\n"
	"arch_rethook_trampoline:\n"
	/* 16 slots: 0..14 = GP regs in HOST order, 15 = the return slot */
	"	subq	$(16*8), %rsp\n"
	"	movq	%r15, (0*8)(%rsp)\n"
	"	movq	%r14, (1*8)(%rsp)\n"
	"	movq	%r13, (2*8)(%rsp)\n"
	"	movq	%r12, (3*8)(%rsp)\n"
	"	movq	%rbp, (4*8)(%rsp)\n"
	"	movq	%rbx, (5*8)(%rsp)\n"
	"	movq	%r11, (6*8)(%rsp)\n"
	"	movq	%r10, (7*8)(%rsp)\n"
	"	movq	%r9,  (8*8)(%rsp)\n"
	"	movq	%r8,  (9*8)(%rsp)\n"
	"	movq	%rax, (10*8)(%rsp)\n"
	"	movq	%rcx, (11*8)(%rsp)\n"
	"	movq	%rdx, (12*8)(%rsp)\n"
	"	movq	%rsi, (13*8)(%rsp)\n"
	"	movq	%rdi, (14*8)(%rsp)\n"
	"	movq	%rsp, %rdi\n"
	"	leaq	(15*8)(%rsp), %rsi\n"
	"	call	arch_rethook_trampoline_callback\n"
	"	movq	(0*8)(%rsp), %r15\n"
	"	movq	(1*8)(%rsp), %r14\n"
	"	movq	(2*8)(%rsp), %r13\n"
	"	movq	(3*8)(%rsp), %r12\n"
	"	movq	(4*8)(%rsp), %rbp\n"
	"	movq	(5*8)(%rsp), %rbx\n"
	"	movq	(6*8)(%rsp), %r11\n"
	"	movq	(7*8)(%rsp), %r10\n"
	"	movq	(8*8)(%rsp), %r9\n"
	"	movq	(9*8)(%rsp), %r8\n"
	"	movq	(10*8)(%rsp), %rax\n"
	"	movq	(11*8)(%rsp), %rcx\n"
	"	movq	(12*8)(%rsp), %rdx\n"
	"	movq	(13*8)(%rsp), %rsi\n"
	"	movq	(14*8)(%rsp), %rdi\n"
	"	addq	$(15*8), %rsp\n"	/* -> slot 15: the real target */
	"	ret\n"
	".size arch_rethook_trampoline, .-arch_rethook_trampoline\n"
);
NOKPROBE_SYMBOL(arch_rethook_trampoline);

/* dump[] slot order = HOST order R15..DI (matches the asm above) */
static const int rethook_dump_order[15] = {
	HOST_R15, HOST_R14, HOST_R13, HOST_R12, HOST_BP, HOST_BX,
	HOST_R11, HOST_R10, HOST_R9, HOST_R8, HOST_AX, HOST_CX,
	HOST_DX, HOST_SI, HOST_DI,
};

__visible void arch_rethook_trampoline_callback(unsigned long *dump,
						unsigned long *frame)
{
	struct pt_regs regs = EMPTY_REGS;
	int i;

	for (i = 0; i < 15; i++)
		regs.regs.gp[rethook_dump_order[i]] = dump[i];
	PT_REGS_SP(&regs) = (unsigned long)frame + 8;
	PT_REGS_IP(&regs) = (unsigned long)arch_rethook_trampoline;

	rethook_trampoline_handler(&regs, (unsigned long)frame);

	/* handlers may have modified registers (e.g. return value) */
	for (i = 0; i < 15; i++)
		dump[i] = regs.regs.gp[rethook_dump_order[i]];

	/* the handler left the real return target in IP */
	*frame = PT_REGS_IP(&regs);
}
NOKPROBE_SYMBOL(arch_rethook_trampoline_callback);

void arch_rethook_prepare(struct rethook_node *rh, struct pt_regs *regs,
			  bool mcount)
{
	unsigned long *stack = (unsigned long *)PT_REGS_SP(regs);

	rh->ret_addr = stack[0];
	rh->frame = (unsigned long)stack;

	/* Replace the return addr with the trampoline addr */
	stack[0] = (unsigned long)arch_rethook_trampoline;
}
NOKPROBE_SYMBOL(arch_rethook_prepare);
