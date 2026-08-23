/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ftrace stubs for UML.
 *
 * With HAVE_FENTRY, gcc -pg instruments every kernel function with a
 * pre-prologue 5-byte `call __fentry__`. UML has no ftrace core backend
 * (no HAVE_DYNAMIC_FTRACE*): those sites serve exclusively as patch
 * targets for BPF fentry/fexit attachment, which pokes them straight to
 * the BPF trampoline via bpf_arch_text_poke(). Unpatched sites call the
 * __fentry__ stub, so all it has to do is return.
 *
 * ftrace_stub exists only because the FUNCTION_TRACER && !DYNAMIC_FTRACE
 * linker-script alias (MCOUNT_REC in asm-generic/vmlinux.lds.h) references
 * it; nothing calls it under UML. arch_ftrace_ops_list_func comes from the
 * generic C fallback in kernel/trace/ftrace.c.
 */
#include <linux/linkage.h>
#include <linux/export.h>

	.text

SYM_FUNC_START(__fentry__)
	ret
SYM_FUNC_END(__fentry__)
EXPORT_SYMBOL(__fentry__)

SYM_FUNC_START(ftrace_stub)
	ret
SYM_FUNC_END(ftrace_stub)
