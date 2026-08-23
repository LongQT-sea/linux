// SPDX-License-Identifier: GPL-2.0
/*
 * set_memory_* for UML: real page protection via the host.
 *
 * A UML guest is a host process; its kernel memory is host-mapped at the
 * same virtual addresses, so protection changes are plain mprotect(2)
 * calls, the same mechanism as the rodata protection in mem.c and the
 * TLB flush path in tlb.c.
 *
 * ROX'd ranges (BPF JIT/trampoline images) are tracked in a small
 * registry so the text_poke path can flip exactly those pages back to
 * writable around a poke and restore ROX after. UML runs UP, so the
 * temporary writable window is not a cross-CPU race; it exists only for
 * the duration of the copy, inside one kernel thread.
 */
#include <linux/mm.h>
#include <linux/set_memory.h>
#include <os.h>

#define MAX_ROX_RANGES 64

static struct {
	unsigned long start, end;
} rox_ranges[MAX_ROX_RANGES];
static int nr_rox_ranges;

static int rox_range_add(unsigned long start, unsigned long end)
{
	if (nr_rox_ranges == MAX_ROX_RANGES)
		return -ENOSPC;
	rox_ranges[nr_rox_ranges].start = start;
	rox_ranges[nr_rox_ranges].end = end;
	nr_rox_ranges++;
	return 0;
}

static void rox_range_del(unsigned long start)
{
	int i;

	for (i = 0; i < nr_rox_ranges; i++) {
		if (rox_ranges[i].start == start) {
			rox_ranges[i] = rox_ranges[--nr_rox_ranges];
			return;
		}
	}
}

bool uml_is_rox_range(unsigned long addr)
{
	int i;

	for (i = 0; i < nr_rox_ranges; i++)
		if (addr >= rox_ranges[i].start && addr < rox_ranges[i].end)
			return true;
	return false;
}

int set_memory_rox(unsigned long addr, int numpages)
{
	int ret = os_protect_memory((void *)addr, numpages << PAGE_SHIFT,
				    1, 0, 1);

	if (!ret)
		ret = rox_range_add(addr, addr + (numpages << PAGE_SHIFT));
	return ret;
}

int set_memory_ro(unsigned long addr, int numpages)
{
	return os_protect_memory((void *)addr, numpages << PAGE_SHIFT,
				 1, 0, 0);
}

int set_memory_rw(unsigned long addr, int numpages)
{
	int ret = os_protect_memory((void *)addr, numpages << PAGE_SHIFT,
				    1, 1, 0);

	if (!ret)
		rox_range_del(addr);
	return ret;
}

int set_memory_x(unsigned long addr, int numpages)
{
	return os_protect_memory((void *)addr, numpages << PAGE_SHIFT,
				 1, 0, 1);
}

int set_memory_nx(unsigned long addr, int numpages)
{
	return os_protect_memory((void *)addr, numpages << PAGE_SHIFT,
				 1, 1, 0);
}

/*
 * Flip a ROX'd range to writable (writable=true) or back to ROX
 * (writable=false) around a text poke. Pages not in the registry are
 * host-RWX already (UML's default), so nothing needs to happen for them.
 */
void uml_text_poke_fixup(unsigned long addr, size_t len, bool writable)
{
	unsigned long start = addr & PAGE_MASK;
	unsigned long end = PAGE_ALIGN(addr + len);

	if (!uml_is_rox_range(start))
		return;

	if (writable)
		os_protect_memory((void *)start, end - start, 1, 1, 1);
	else
		os_protect_memory((void *)start, end - start, 1, 0, 1);
}

/*
 * Write to kernel text (the BPF fentry attach/detach path). Unlike JIT
 * images, kernel text is r-x from the host loader and is not in the ROX
 * registry, so the writable window must be created here: flip the covered
 * page(s) to RWX, copy, flip back to R-X.
 */
void uml_kernel_text_poke(void *addr, const void *opcode, size_t len)
{
	unsigned long start = (unsigned long)addr & PAGE_MASK;
	unsigned long end = PAGE_ALIGN((unsigned long)addr + len);

	os_protect_memory((void *)start, end - start, 1, 1, 1);
	memcpy(addr, opcode, len);
	os_protect_memory((void *)start, end - start, 1, 0, 1);
}
