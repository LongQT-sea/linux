// SPDX-License-Identifier: GPL-2.0-only
/*
 * Instruction patching for the arm64 UML backend.
 *
 * arch/arm64/kernel/module.c is reused here to relocate modules, and
 * it writes the relocated instructions through aarch64_insn_copy(). On
 * hardware that function exists because kernel text is mapped
 * read-only: it creates a writable alias of the target page, copies
 * through it, and then performs the cache maintenance that makes the
 * new instructions visible to the instruction fetch path.
 *
 * A UML guest has no read-only kernel text to work around; module
 * memory is ordinary writable memory in an ordinary process, so the
 * writable-alias half of the problem does not exist and a plain memcpy
 * is correct.
 *
 * The cache-maintenance half very much still exists, and is the reason
 * this is not simply a memcpy. These are real instructions being
 * written for a real arm64 CPU with separate, non-coherent instruction
 * and data caches. Without cleaning the new bytes to the point of
 * unification and invalidating the I-cache for the range, the CPU may
 * execute whatever the I-cache held before, and the failure would be
 * an intermittent crash inside a freshly loaded module, dependent on
 * cache pressure.
 *
 * __builtin___clear_cache() is the right primitive rather than arm64's
 * caches_clean_inval_pou(): that one lives in arch/arm64/mm/cache.S,
 * which is not built for ARCH=um, and is written for EL1. The compiler
 * builtin emits the EL0-legal sequence (DC CVAU / DSB ISH / IC IVAU /
 * DSB ISH / ISB), which is exactly what a userspace JIT does and
 * exactly what this is.
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/text-patching.h>

void *aarch64_insn_copy(void *dst, void *src, size_t len)
{
	memcpy(dst, src, len);

	__builtin___clear_cache((char *)dst, (char *)dst + len);

	return dst;
}
