// SPDX-License-Identifier: GPL-2.0
/*
 * UML's own setjmp/longjmp for arm64.
 * Layout (must match sysdep/archsetjmp.h):
 *   x19/20 @0, x21/22 @16, x23/24 @32, x25/26 @48,
 *   x27/28 @64, fp/lr @80, sp @96, pc @104.
 */
#include <linux/linkage.h>

.text

/* int kernel_setjmp(jmp_buf *buf): x0 = &buf[0]; returns 0 */
SYM_FUNC_START(kernel_setjmp)
	stp	x19, x20, [x0, #0]
	stp	x21, x22, [x0, #16]
	stp	x23, x24, [x0, #32]
	stp	x25, x26, [x0, #48]
	stp	x27, x28, [x0, #64]
	stp	x29, x30, [x0, #80]
	mov	x1, sp
	str	x1, [x0, #96]
	mov	x1, x30
	str	x1, [x0, #104]
	mov	x0, #0
	ret
SYM_FUNC_END(kernel_setjmp)

/*
 * void kernel_longjmp(jmp_buf *buf, int val): x0 = &buf[0], w1 = val;
 * the restored context sees val (1 if val == 0) as setjmp's return.
 */
SYM_FUNC_START(kernel_longjmp)
	ldp	x19, x20, [x0, #0]
	ldp	x21, x22, [x0, #16]
	ldp	x23, x24, [x0, #32]
	ldp	x25, x26, [x0, #48]
	ldp	x27, x28, [x0, #64]
	ldp	x29, x30, [x0, #80]
	ldr	x2, [x0, #96]
	mov	sp, x2
	ldr	x2, [x0, #104]
	cmp	w1, #0
	csinc	w1, w1, wzr, ne
	mov	w0, w1
	br	x2
SYM_FUNC_END(kernel_longjmp)
