// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched/signal.h>

#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <as-layout.h>
#include <mem_user.h>
#include <os.h>
#include <skas.h>
#include <kern_util.h>

struct vm_ops {
	struct mm_id *mm_idp;

	int (*mmap)(struct mm_id *mm_idp,
		    unsigned long virt, unsigned long len, int prot,
		    int phys_fd, unsigned long long offset);
	int (*unmap)(struct mm_id *mm_idp,
		     unsigned long virt, unsigned long len);
};

static int kern_map(struct mm_id *mm_idp,
		    unsigned long virt, unsigned long len, int prot,
		    int phys_fd, unsigned long long offset)
{
	return os_map_memory((void *)virt, phys_fd, offset, len,
			     prot & UM_PROT_READ, prot & UM_PROT_WRITE,
			     prot & UM_PROT_EXEC);
}

static int kern_unmap(struct mm_id *mm_idp,
		      unsigned long virt, unsigned long len)
{
	return os_unmap_memory((void *)virt, len);
}

/*
 * The page tables are walked without the page table locks, like a hardware
 * walker would: other CPUs may change entries under their (split) locks
 * meanwhile. Each entry is read once, the host is updated from that snapshot,
 * and _PAGE_NEEDSYNC is then only cleared if the entry still holds the
 * snapshot. Every update of an entry sets _PAGE_NEEDSYNC again (set_pte(),
 * pte_clear(), pmd_clear(), ...), and is followed by marking the range
 * (set_ptes() or a TLB flush), which takes sync_tlb_lock and so waits for
 * this walk; so a lost race only means the entry is synced again later.
 *
 * Page table pages are only freed after a TLB flush of their range, which
 * also takes sync_tlb_lock, so they cannot go away during a walk.
 *
 * A present upper level entry can still carry _PAGE_NEEDSYNC, if a table was
 * freed and a new one installed before the range was synced (see
 * um_populate_entry()). Then the host may still map pages of the old table,
 * so the range is unmapped and everything present below is mapped again
 * ("force").
 */
static inline void entry_mkuptodate(unsigned long *entry, unsigned long val)
{
	cmpxchg(entry, val, val & ~_PAGE_NEEDSYNC);
}

static inline int update_pte_range(pmd_t pmdval, unsigned long addr,
				   unsigned long end,
				   struct vm_ops *ops, bool force)
{
	pte_t *pte;
	int ret = 0;

	pte = (pte_t *)pmd_page_vaddr(pmdval) + pte_index(addr);
	do {
		pte_t val = ptep_get(pte);

		if (!force && !pte_needsync(val))
			continue;

		if (pte_present(val)) {
			__u64 offset;
			unsigned long phys = pte_val(val) & PAGE_MASK;
			int fd = phys_mapping(phys, &offset);
			int r, w, x, prot;

			r = pte_read(val);
			w = pte_write(val);
			x = pte_exec(val);
			if (!pte_young(val)) {
				r = 0;
				w = 0;
			} else if (!pte_dirty(val)) {
				w = 0;
			}

			prot = (r ? UM_PROT_READ : 0) |
			       (w ? UM_PROT_WRITE : 0) |
			       (x ? UM_PROT_EXEC : 0);

			ret = ops->mmap(ops->mm_idp, addr, PAGE_SIZE,
					prot, fd, offset);
		} else if (!force)
			ret = ops->unmap(ops->mm_idp, addr, PAGE_SIZE);

		if (pte_needsync(val))
			entry_mkuptodate(&pte_val(*pte), pte_val(val));
	} while (pte++, addr += PAGE_SIZE, ((addr < end) && !ret));
	return ret;
}

/*
 * Folded levels have no table of their own: the entry of a folded level is
 * the one of the next level, at the same place. Real levels are reached from
 * the snapshot of the entry above, which may have changed meanwhile.
 */
static inline pud_t *pud_entry_ptr(p4d_t *p4dp, p4d_t p4dval,
				   unsigned long addr)
{
#ifdef __PAGETABLE_PUD_FOLDED
	return pud_offset(p4dp, addr);
#else
	return p4d_pgtable(p4dval) + pud_index(addr);
#endif
}

static inline pmd_t *pmd_entry_ptr(pud_t *pudp, pud_t pudval,
				   unsigned long addr)
{
#ifdef __PAGETABLE_PMD_FOLDED
	return pmd_offset(pudp, addr);
#else
	return pud_pgtable(pudval) + pmd_index(addr);
#endif
}

/* _PAGE_NEEDSYNC of a folded level is the one of the next level */
static inline bool p4d_entry_needsync(p4d_t p4dval)
{
#ifdef __PAGETABLE_PUD_FOLDED
	return false;
#else
	return p4d_needsync(p4dval);
#endif
}

static inline bool pud_entry_needsync(pud_t pudval)
{
#ifdef __PAGETABLE_PMD_FOLDED
	return false;
#else
	return pud_needsync(pudval);
#endif
}

static inline int update_pmd_range(pud_t *pudp, pud_t pudval,
				   unsigned long addr, unsigned long end,
				   struct vm_ops *ops, bool force)
{
	pmd_t *pmd;
	unsigned long next;
	int ret = 0;

	pmd = pmd_entry_ptr(pudp, pudval, addr);
	do {
		pmd_t val = READ_ONCE(*pmd);
		bool sync = pmd_needsync(val);

		next = pmd_addr_end(addr, end);
		if (sync && !force)
			ret = ops->unmap(ops->mm_idp, addr, next - addr);
		if (!ret && pmd_present(val))
			ret = update_pte_range(val, addr, next, ops,
					       force || sync);
		if (sync)
			entry_mkuptodate(&pmd_val(*pmd), pmd_val(val));
	} while (pmd++, addr = next, ((addr < end) && !ret));
	return ret;
}

static inline int update_pud_range(p4d_t *p4dp, p4d_t p4dval,
				   unsigned long addr, unsigned long end,
				   struct vm_ops *ops, bool force)
{
	pud_t *pud;
	unsigned long next;
	int ret = 0;

	pud = pud_entry_ptr(p4dp, p4dval, addr);
	do {
		pud_t val = READ_ONCE(*pud);
		bool sync = pud_entry_needsync(val);

		next = pud_addr_end(addr, end);
		if (sync && !force)
			ret = ops->unmap(ops->mm_idp, addr, next - addr);
		if (!ret && pud_present(val))
			ret = update_pmd_range(pud, val, addr, next, ops,
					       force || sync);
		if (sync)
			entry_mkuptodate(&pud_val(*pud), pud_val(val));
	} while (pud++, addr = next, ((addr < end) && !ret));
	return ret;
}

static inline int update_p4d_range(pgd_t *pgd, unsigned long addr,
				   unsigned long end,
				   struct vm_ops *ops)
{
	p4d_t *p4d;
	unsigned long next;
	int ret = 0;

	p4d = p4d_offset(pgd, addr);
	do {
		p4d_t val = READ_ONCE(*p4d);
		bool sync = p4d_entry_needsync(val);

		next = p4d_addr_end(addr, end);
		if (sync)
			ret = ops->unmap(ops->mm_idp, addr, next - addr);
		if (!ret && p4d_present(val))
			ret = update_pud_range(p4d, val, addr, next, ops, sync);
		if (sync)
			entry_mkuptodate(&p4d_val(*p4d), p4d_val(val));
	} while (p4d++, addr = next, ((addr < end) && !ret));
	return ret;
}

static int __um_tlb_sync(struct mm_struct *mm, struct vm_ops *ops)
{
	pgd_t *pgd;
	unsigned long addr, next;
	int ret = 0;

	if (mm->context.sync_tlb_range_to == 0)
		return 0;

	addr = mm->context.sync_tlb_range_from;
	pgd = pgd_offset(mm, addr);
	do {
		next = pgd_addr_end(addr, mm->context.sync_tlb_range_to);
		if (!pgd_present(*pgd)) {
			if (pgd_needsync(*pgd)) {
				ret = ops->unmap(ops->mm_idp, addr,
						 next - addr);
				pgd_mkuptodate(*pgd);
			}
		} else
			ret = update_p4d_range(pgd, addr, next, ops);
	} while (pgd++, addr = next,
		 ((addr < mm->context.sync_tlb_range_to) && !ret));

	if (ret == -ENOMEM)
		report_enomem();

	mm->context.sync_tlb_range_from = 0;
	mm->context.sync_tlb_range_to = 0;

	return ret;
}

int um_tlb_sync(struct mm_struct *mm)
{
	struct vm_ops ops;

	if (mm != &init_mm) {
		ops.mm_idp = &mm->context.id;
		ops.mmap = map;
		ops.unmap = unmap;

		guard(spinlock_irqsave)(&mm->context.sync_tlb_lock);
		return __um_tlb_sync(mm, &ops);
	}

	ops.mm_idp = NULL;
	ops.mmap = kern_map;
	ops.unmap = kern_unmap;

	guard(spinlock_irqsave)(&mm->page_table_lock);
	guard(spinlock_irqsave)(&mm->context.sync_tlb_lock);

	return __um_tlb_sync(mm, &ops);
}

void flush_tlb_all(void)
{
	/*
	 * Don't bother flushing if this address space is about to be
	 * destroyed.
	 */
	if (atomic_read(&current->mm->mm_users) == 0)
		return;

	flush_tlb_mm(current->mm);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, 0);

	for_each_vma(vmi, vma)
		um_tlb_mark_sync(mm, vma->vm_start, vma->vm_end);
}
