/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Symbol aliases for the UML/s390x syscall table.
 *
 * Included immediately before <asm/syscall_table.h> in both passes of
 * sys_call_table.c so the aliased slots resolve to sys_ni_syscall at
 * declaration AND table-fill time. Loud -ENOSYS beats silent breakage;
 * revisit only if the ladder ever exercises these paths.
 *
 * - Legacy signal calls: s390 libc (glibc >= 2.4, musl) programs use
 *   SA_RESTORER + rt_sigreturn exclusively (DESIGN §5); nothing sane
 *   calls sys_sigaction/sys_sigsuspend/sys_sigreturn any more.
 * - Facility-gated hardware calls: runtime instrumentation,
 *   guarded storage, and STHYI report host facilities UML does not
 *   virtualize; native kernels return EOPNOTSUPP/ENOSYS without the
 *   facility bits, which UML never sets.
 */
#define sys_sigaction		sys_ni_syscall
#define sys_sigsuspend		sys_ni_syscall
#define sys_sigreturn		sys_ni_syscall
#define sys_s390_runtime_instr	sys_ni_syscall
#define sys_s390_guarded_storage sys_ni_syscall
#define sys_s390_sthyi		sys_ni_syscall
