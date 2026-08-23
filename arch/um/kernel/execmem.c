// SPDX-License-Identifier: GPL-2.0
/*
 * execmem arch setup for UML.
 *
 * BPF JIT images reach kernel text with rel32 (±2 GB) calls. The generic
 * execmem fallback allocates over [VMALLOC_START, VMALLOC_END), which
 * under UML slides up with the guest's physmem, past rel32 reach of
 * kernel text (0x60000000) once mem= exceeds ~1.5 GB. Allocate the BPF
 * range from a dedicated window directly below kernel text instead:
 * that VA is unused in the UML kernel process and stays within ±2 GB of
 * text regardless of physmem size. Everything else (modules, etc.)
 * keeps the generic vmalloc-space behavior via EXECMEM_DEFAULT.
 */
#include <linux/execmem.h>
#include <linux/init.h>
#include <asm/pgtable.h>

/* 256 MB window immediately below the kernel image (0x60000000) */
#define UML_EXECMEM_BPF_START	0x50000000UL
#define UML_EXECMEM_BPF_END	0x60000000UL

/*
 * kprobe out-of-line slots share the constraint: the boost path's
 * trailing jmp rel32 must reach the probe site, and a vmalloc-range
 * slot is out of reach once mem= is large. Give them the adjacent
 * 256 MB window.
 */
#define UML_EXECMEM_KPROBES_START	0x40000000UL
#define UML_EXECMEM_KPROBES_END	0x50000000UL

static struct execmem_info um_execmem_info = {
	.ranges = {
		[EXECMEM_BPF] = {
			.start		= UML_EXECMEM_BPF_START,
			.end		= UML_EXECMEM_BPF_END,
			.pgprot		= PAGE_KERNEL_EXEC,
			.alignment	= 1,
		},
		[EXECMEM_KPROBES] = {
			.start		= UML_EXECMEM_KPROBES_START,
			.end		= UML_EXECMEM_KPROBES_END,
			.pgprot		= PAGE_KERNEL_EXEC,
			.alignment	= 1,
		},
	},
};

struct execmem_info * __init execmem_arch_setup(void)
{
	/* VMALLOC_START/END are not compile-time constants under UML */
	um_execmem_info.ranges[EXECMEM_DEFAULT].start = VMALLOC_START;
	um_execmem_info.ranges[EXECMEM_DEFAULT].end = VMALLOC_END;
	um_execmem_info.ranges[EXECMEM_DEFAULT].pgprot = PAGE_KERNEL_EXEC;
	um_execmem_info.ranges[EXECMEM_DEFAULT].alignment = 1;

	return &um_execmem_info;
}
