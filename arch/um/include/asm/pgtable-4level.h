/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2003 PathScale Inc
 * Derived from include/asm-i386/pgtable.h
 */

#ifndef __UM_PGTABLE_4LEVEL_H
#define __UM_PGTABLE_4LEVEL_H

#include <asm-generic/pgtable-nop4d.h>

/*
 * The level geometry derives from PAGE_SHIFT: every page-table page is
 * one page long and holds PAGE_SIZE/8 entries, i.e. (PAGE_SHIFT - 3) bits
 * of address space per level.  At PAGE_SIZE=4K this reproduces the
 * historical hardcoded constants (512 entries, shifts 21/30/39) exactly;
 * at 16K it yields 2048 entries and shifts 25/36/47.
 *
 * The hardcoded constants were silently inconsistent once UML grew a
 * PAGE_SIZE > 4K backend: with PMD_SHIFT=21 and PTRS_PER_PTE=512 hardcoded,
 * a 16K page size makes pte_index() span 9 bits while a pmd covers only
 * 2MB = 128 pages, so pte table slots >= 128 alias the following pmds'
 * address ranges.  Batch fault paths (do_fault_around clamps its window by
 * PTRS_PER_PTE) then install counted ptes into such phantom slots, which no
 * address-driven walk (zap, unmap, exit) can reach; the rss accounting
 * leaks by exactly the number of batch pages beyond the pmd boundary
 * (observed as "Bad rss-counter state type:MM_FILEPAGES val:3").
 */

/* log2 of entries per table page: 8 bytes per entry, one page per table */
#define UML_PGTABLE_LEVEL_BITS	(PAGE_SHIFT - 3)

/* PGDIR_SHIFT determines what a fourth-level page table entry can map */

#define PGDIR_SHIFT	(PAGE_SHIFT + 3 * UML_PGTABLE_LEVEL_BITS)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* PUD_SHIFT determines the size of the area a third-level page table can
 * map
 */

#define PUD_SHIFT	(PAGE_SHIFT + 2 * UML_PGTABLE_LEVEL_BITS)
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))

/* PMD_SHIFT determines the size of the area a second-level page table can
 * map
 */

#define PMD_SHIFT	(PAGE_SHIFT + UML_PGTABLE_LEVEL_BITS)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/*
 * entries per page directory level
 */

#define PTRS_PER_PTE	(1 << UML_PGTABLE_LEVEL_BITS)
#define PTRS_PER_PMD	(1 << UML_PGTABLE_LEVEL_BITS)
#define PTRS_PER_PUD	(1 << UML_PGTABLE_LEVEL_BITS)
#define PTRS_PER_PGD	(1 << UML_PGTABLE_LEVEL_BITS)

#define USER_PTRS_PER_PGD ((TASK_SIZE + (PGDIR_SIZE - 1)) / PGDIR_SIZE)

#define pte_ERROR(e) \
        printk("%s:%d: bad pte %p(%016lx).\n", __FILE__, __LINE__, &(e), \
	       pte_val(e))
#define pmd_ERROR(e) \
        printk("%s:%d: bad pmd %p(%016lx).\n", __FILE__, __LINE__, &(e), \
	       pmd_val(e))
#define pud_ERROR(e) \
        printk("%s:%d: bad pud %p(%016lx).\n", __FILE__, __LINE__, &(e), \
	       pud_val(e))
#define pgd_ERROR(e) \
        printk("%s:%d: bad pgd %p(%016lx).\n", __FILE__, __LINE__, &(e), \
	       pgd_val(e))

#define pud_none(x)	(!(pud_val(x) & ~_PAGE_NEEDSYNC))
#define	pud_bad(x)	((pud_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)
#define pud_present(x)	(pud_val(x) & _PAGE_PRESENT)
#define pud_populate(mm, pud, pmd) \
	set_pud(pud, __pud(_PAGE_TABLE + __pa(pmd)))

#define set_pud(pudptr, pudval) (*(pudptr) = (pudval))

#define p4d_none(x)	(!(p4d_val(x) & ~_PAGE_NEEDSYNC))
#define	p4d_bad(x)	((p4d_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)
#define p4d_present(x)	(p4d_val(x) & _PAGE_PRESENT)
#define p4d_populate(mm, p4d, pud) \
	set_p4d(p4d, __p4d(_PAGE_TABLE + __pa(pud)))

#define set_p4d(p4dptr, p4dval) (*(p4dptr) = (p4dval))


static inline int pgd_needsync(pgd_t pgd)
{
	return pgd_val(pgd) & _PAGE_NEEDSYNC;
}

static inline void pgd_mkuptodate(pgd_t pgd) { pgd_val(pgd) &= ~_PAGE_NEEDSYNC; }

#define set_pmd(pmdptr, pmdval) (*(pmdptr) = (pmdval))

static inline void pud_clear (pud_t *pud)
{
	set_pud(pud, __pud(_PAGE_NEEDSYNC));
}

static inline void p4d_clear (p4d_t *p4d)
{
	set_p4d(p4d, __p4d(_PAGE_NEEDSYNC));
}

#define pud_page(pud) phys_to_page(pud_val(pud) & PAGE_MASK)
#define pud_pgtable(pud) ((pmd_t *) __va(pud_val(pud) & PAGE_MASK))

#define p4d_page(p4d) phys_to_page(p4d_val(p4d) & PAGE_MASK)
#define p4d_pgtable(p4d) ((pud_t *) __va(p4d_val(p4d) & PAGE_MASK))

static inline unsigned long pte_pfn(pte_t pte)
{
	return phys_to_pfn(pte_val(pte));
}

static inline pmd_t pfn_pmd(unsigned long page_nr, pgprot_t pgprot)
{
	return __pmd((page_nr << PAGE_SHIFT) | pgprot_val(pgprot));
}

#endif
