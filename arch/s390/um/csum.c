// SPDX-License-Identifier: GPL-2.0
/*
 * Plain-C checksum for the s390x UML backend.
 *
 * The guest CPU *is* the host Z CPU, but the CKSM-based native
 * csum-partial.o drags in kernel_fpu/thread_struct machinery that has
 * no meaning under UML (guest "threads" are host processes; there is
 * no guest FPU state to save). The static helpers (cksm, fold, magic)
 * in <asm/checksum.h> are plain C and stay shared; only the two
 * out-of-line entry points live here. Byte-order neutral: folded sums
 * are endian-symmetric by construction [P5].
 */
#include <linux/export.h>
#include <linux/string.h>
#include <asm/checksum.h>

__wsum csum_partial(const void *buff, int len, __wsum sum)
{
	const u16 *buf = (const u16 *)buff;
	u64 acc = (__force u32)sum;

	/*
	 * Accumulate into 64 bits and fold once at the end: a u32
	 * accumulator drops end-around carries on large buffers or
	 * large incoming partial sums (NFS/iSCSI sizes).
	 */
	while (len > 1) {
		acc += *buf++;
		len -= 2;
	}
	if (len == 1)
		acc += (u64)(*(const u8 *)buf) << 8; /* BE */
	acc = (acc >> 32) + (u32)acc;
	acc += acc >> 32;
	sum = (__force __wsum)(u32)acc;
	sum = (__force __wsum)(((__force u32)sum >> 16) +
			       ((__force u32)sum & 0xffff));
	if ((__force u32)sum > 0xffff)
		sum -= (__force __wsum)0xffff;
	return sum;
}
EXPORT_SYMBOL(csum_partial);

__wsum csum_partial_copy_nocheck(const void *src, void *dst, int len)
{
	memcpy(dst, src, len);
	return csum_partial(dst, len, 0);
}
EXPORT_SYMBOL(csum_partial_copy_nocheck);
