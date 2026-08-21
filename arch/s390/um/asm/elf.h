/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ELF definitions for the s390x UML backend — EM_S390 model with the
 * UML register layout (psw, gprs, acrs, orig_gpr2) and the um vDSO
 * auxv hook. Big-endian: ELF_DATA is ELFDATA2MSB (first BE UML).
 */
#ifndef __UM_S390_ELF_H
#define __UM_S390_ELF_H

#include <asm/ptrace.h>
#include <asm/user.h>
#include <skas.h>
#include <linux/string.h>

#define CORE_DUMP_USE_REGSET

#define R_390_NONE		0
#define R_390_64		22
#define R_390_PC64		24
#define R_390_GLOB_DAT		26
#define R_390_COPY		27
#define R_390_JUMP_SLOT		28
#define R_390_RELATIVE		29
#define R_390_32		4
#define R_390_PC32		5
#define R_390_GOTPCREL		26

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) \
	((x)->e_machine == EM_S390)

#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2MSB
#define ELF_ARCH	EM_S390

typedef unsigned long elf_greg_t;

#define ELF_PLAT_INIT(regs, load_addr)    do { \
	int _i; \
	for (_i = 0; _i < 16; _i++) \
		(regs)->regs.gp[HOST_GPR0 + _i] = 0; \
	(regs)->regs.gp[HOST_TLS] = 0; /* exec resets the TLS pointer */ \
} while (0)

static inline void um_elf_core_copy_regs(elf_greg_t *pr_reg,
					 struct pt_regs *_regs)
{
	pr_reg[0] = (_regs)->regs.gp[HOST_PSW_MASK];
	pr_reg[1] = (_regs)->regs.gp[HOST_PSW_ADDR];
	memcpy(&pr_reg[2], &(_regs)->regs.gp[HOST_GPR0], 16 * sizeof(long));
}

#define ELF_CORE_COPY_REGS(pr_reg, _regs) um_elf_core_copy_regs(pr_reg, _regs);

#define ELF_PLATFORM_FALLBACK "s390x"

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
struct linux_binprm;
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
	int uses_interp);

extern unsigned long um_vdso_addr;
extern unsigned long um_minsigstksz;
/*
 * ARCH_DLINFO ships two NEW_AUX_ENT entries (vdso + minsigstksz).
 * Native s390 sets AT_VECTOR_SIZE_ARCH = 1 in uapi auxvec.h; the
 * extra entry is covered by AT_VECTOR_SIZE's +1 slack, exactly as
 * x86_64 UM does with AT_VECTOR_SIZE_ARCH unset. Do NOT redefine it
 * here — that changes sizeof(mm->saved_auxv) and breaks the
 * prctl_set_auxv BUILD_BUG_ON.
 */
#define ARCH_DLINFO						      \
do {								      \
	NEW_AUX_ENT(AT_SYSINFO_EHDR, um_vdso_addr);		      \
	NEW_AUX_ENT(AT_MINSIGSTKSZ, um_minsigstksz);		      \
} while (0)

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof(struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct user_fpregs_struct elf_fpregset_t;

struct task_struct;

/*
 * AT_PAGESZ must tell the truth (F58 lesson): s390x is always 4K, so
 * PAGE_SIZE is correct by construction.
 */
#define ELF_EXEC_PAGESIZE PAGE_SIZE

#define ELF_ET_DYN_BASE ((TASK_SIZE / 3 * 2) & ~((1UL << 32) - 1))

extern long elf_aux_hwcap;
/*
 * elf_aux_hwcap mirrors the host's AT_HWCAP (shared
 * arch/um/os-Linux/elf_aux.c), minus the facilities whose CPU state
 * the port cannot round-trip across stub traps and signal frames
 * (only the 136-byte fp frame is preserved): the vector family and
 * its extensions (HWCAP_NR_VXRS 11, _BCD 12, _EXT 13, _EXT2 15,
 * _PDE 16, _PDE2 19), guarded storage (14) and NNPA (20). Guests
 * told "vx" would use vector registers and silently corrupt state
 * between tasks (native derives these from facilities 129/134/135/
 * 148/152/192/133/165). Wiring NT_S390_VXRS_* + _sigregs_ext is the
 * follow-up that unmasks them.
 */
#define UM_S390_HWCAP_MASK					      \
	((1UL << 11) | (1UL << 12) | (1UL << 13) | (1UL << 14) |     \
	 (1UL << 15) | (1UL << 16) | (1UL << 19) | (1UL << 20))
#define ELF_HWCAP (elf_aux_hwcap & ~UM_S390_HWCAP_MASK)

extern char *elf_aux_platform;
#define ELF_PLATFORM (elf_aux_platform ?: ELF_PLATFORM_FALLBACK)

#endif
