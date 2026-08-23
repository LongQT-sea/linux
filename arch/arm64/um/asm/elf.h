/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ELF definitions for the arm64 UML backend: EM_AARCH64 model with
 * the UML register layout (x0..x30, sp, pc, pstate) and the um vDSO
 * auxv hook.
 */
#ifndef __UM_ARM64_ELF_H
#define __UM_ARM64_ELF_H

#include <asm/ptrace.h>
#include <asm/user.h>
#include <skas.h>

#define CORE_DUMP_USE_REGSET

#define R_AARCH64_NONE		0
#define R_AARCH64_ABS64		257
#define R_AARCH64_ABS32		258
#define R_AARCH64_ABS16		259
#define R_AARCH64_PREL64	260
#define R_AARCH64_PREL32	261
#define R_AARCH64_PREL16	262
#define R_AARCH64_ADR_PREL_PG_HI21	275
#define R_AARCH64_ADD_ABS_LO12_NC	277
#define R_AARCH64_JUMP26	282
#define R_AARCH64_CALL26	283
#define R_AARCH64_COPY		1024
#define R_AARCH64_GLOB_DAT	1025
#define R_AARCH64_JUMP_SLOT	1026
#define R_AARCH64_RELATIVE	1027

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) \
	((x)->e_machine == EM_AARCH64)

#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_AARCH64

typedef unsigned long elf_greg_t;

#define ELF_PLAT_INIT(regs, load_addr)    do { \
	int _i; \
	for (_i = 0; _i < 31; _i++) \
		(regs)->regs.gp[_i] = 0; \
	(regs)->regs.gp[HOST_TLS] = 0; /* exec resets TPIDR_EL0 */ \
} while (0)

static inline void um_elf_core_copy_regs(elf_greg_t *pr_reg,
					 struct pt_regs *_regs)
{
	int i;

	for (i = 0; i < 31; i++)
		pr_reg[i] = (_regs)->regs.gp[i];
	pr_reg[31] = (_regs)->regs.gp[HOST_SP];
	pr_reg[32] = (_regs)->regs.gp[HOST_PC];
	pr_reg[33] = (_regs)->regs.gp[HOST_PSTATE];
}

#define ELF_CORE_COPY_REGS(pr_reg, _regs) um_elf_core_copy_regs(pr_reg, _regs);

#define ELF_PLATFORM_FALLBACK "aarch64"

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES
struct linux_binprm;
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
	int uses_interp);

extern unsigned long um_vdso_addr;
extern unsigned long um_minsigstksz;
#define AT_SYSINFO_EHDR 33
#define ARCH_DLINFO \
	NEW_AUX_ENT(AT_SYSINFO_EHDR, um_vdso_addr); \
	NEW_AUX_ENT(AT_MINSIGSTKSZ, um_minsigstksz)
/* NEW_AUX_ENT count in ARCH_DLINFO (x86 UM leaves this 0 and relies on slack) */
#define AT_VECTOR_SIZE_ARCH 2

#define ELF_NGREG (sizeof(struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

struct user_fpsimd_struct;
typedef struct user_fpsimd_struct elf_fpregset_t;

struct task_struct;

/*
 * AT_PAGESZ must tell the truth: userspace (ld.so's map-hole and
 * relro mprotects) sizes its memory syscalls from ELF_EXEC_PAGESIZE,
 * and the guest mm rejects alignments below PAGE_SIZE.  The x86 UM
 * template's hardcoded 4096 is correct there (UML-x86 is always 4K);
 * with configurable arm64 guest pages it must follow PAGE_SIZE, as
 * native arm64 does.  A 4096 lie on a 16K guest kills every dynamic
 * binary at ld.so map time, and any static whose startup runs a
 * relro mprotect.
 */
#define ELF_EXEC_PAGESIZE PAGE_SIZE

#define ELF_ET_DYN_BASE (TASK_SIZE / 3 * 2)

extern long elf_aux_hwcap;
/*
 * elf_aux_hwcap mirrors the host's AT_HWCAP verbatim (shared
 * arch/um/os-Linux/elf_aux.c). Mask out what the port cannot honor:
 *
 * - PACA/PACG (bits 30/31): the port's stance is that the guest sees
 *   a non-PAC CPU. PAC keys do not survive a guest fork (each stub
 *   process gets fresh host keys), and stray cross-process PAC
 *   instructions are only emulated on the SIGILL path; a PAC-aware
 *   guest libc or JIT trusting the hwcap would exercise the emulator
 *   at scale.
 *
 * - SVE (bit 22): only the 528-byte fpsimd record round-trips through
 *   the signal frames and regsets, so on an SVE host the Z/P upper
 *   state would leak between guest tasks across context switches.
 *   The guest must not be told SVE exists. (No AT_HWCAP2 is
 *   advertised at all, ELF_HWCAP2 is undefined here, so the SVE2
 *   subfeature bits never reach the guest.)
 *
 * Bit numbers are uapi HWCAP_*; the uapi header is deliberately not
 * included here.
 */
#define ELF_HWCAP (elf_aux_hwcap & \
		   ~((1UL << 30) | (1UL << 31) | (1UL << 22)))

extern char *elf_aux_platform;
#define ELF_PLATFORM (elf_aux_platform ?: ELF_PLATFORM_FALLBACK)

#define SET_PERSONALITY(ex) do {} while (0)

#endif
