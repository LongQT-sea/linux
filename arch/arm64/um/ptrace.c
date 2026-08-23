// SPDX-License-Identifier: GPL-2.0
/*
 * Register access and regset view for the arm64 UML backend:
 * getreg/putreg over the UML register frame (x0..x30, sp, pc, pstate
 * with the NZCV write mask), and the core-dump regset view
 * (NT_PRSTATUS + NT_PRFPREG).
 */
#include <linux/sched.h>
#include <linux/elf.h>
#include <linux/regset.h>
#include <linux/uaccess.h>
#include <asm/ptrace.h>

unsigned long getreg(struct task_struct *child, int regno)
{
	return task_pt_regs(child)->regs.gp[regno / sizeof(long)];
}

int putreg(struct task_struct *child, int regno, unsigned long value)
{
	/* Only the flag and hint bits of PSTATE are userspace-writable */
	if (regno / sizeof(long) == HOST_PSTATE)
		value = (value & UM_PSTATE_WRITABLE) |
			(task_pt_regs(child)->regs.gp[HOST_PSTATE] &
			 ~(unsigned long)UM_PSTATE_WRITABLE);

	task_pt_regs(child)->regs.gp[regno / sizeof(long)] = value;
	return 0;
}

static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       struct membuf to)
{
	int reg;

	for (reg = 0; to.left; reg++)
		membuf_store(&to, getreg(target, reg * sizeof(long)));
	return 0;
}

static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	int ret = 0;

	if (kbuf) {
		const unsigned long *k = kbuf;

		while (count >= sizeof(*k) && !ret) {
			ret = putreg(target, pos, *k++);
			count -= sizeof(*k);
			pos += sizeof(*k);
		}
	} else {
		const unsigned long  __user *u = ubuf;

		while (count >= sizeof(*u) && !ret) {
			unsigned long word;

			ret = __get_user(word, u++);
			if (ret)
				break;
			ret = putreg(target, pos, word);
			count -= sizeof(*u);
			pos += sizeof(*u);
		}
	}
	return ret;
}

/*
 * regs.fp holds the canonical user_fpsimd_state layout {vregs, fpsr,
 * fpcr} in both ptrace and seccomp mode (the seccomp mcontext marshal
 * converts from sigcontext record order at its boundary), so the
 * NT_PRFPREG regset can serve the bytes directly.
 */
static int generic_fpregs_active(struct task_struct *target,
				 const struct user_regset *regset)
{
	return regset->n;
}

static int generic_fpregs_get(struct task_struct *target,
			      const struct user_regset *regset,
			      struct membuf to)
{
	void *fpregs = task_pt_regs(target)->regs.fp;

	membuf_write(&to, fpregs, regset->size * regset->n);
	return 0;
}

static int generic_fpregs_set(struct task_struct *target,
			      const struct user_regset *regset,
			      unsigned int pos, unsigned int count,
			      const void *kbuf, const void __user *ubuf)
{
	void *fpregs = task_pt_regs(target)->regs.fp;

	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  fpregs, 0, regset->size * regset->n);
}

static struct user_regset uml_regsets[] __ro_after_init = {
	[REGSET_GENERAL] = {
		USER_REGSET_NOTE_TYPE(PRSTATUS),
		.n		= sizeof(struct user_regs_struct) / sizeof(long),
		.size		= sizeof(long),
		.align		= sizeof(long),
		.regset_get	= genregs_get,
		.set		= genregs_set
	},
	[REGSET_FP] = {
		USER_REGSET_NOTE_TYPE(PRFPREG),
		.n		= sizeof(struct user_fpsimd_struct) / sizeof(long),
		.size		= sizeof(long),
		.align		= sizeof(long),
		.active		= generic_fpregs_active,
		.regset_get	= generic_fpregs_get,
		.set		= generic_fpregs_set,
	},
};

static const struct user_regset_view user_uml_view = {
	.name = "aarch64", .e_machine = EM_AARCH64,
	.regsets = uml_regsets, .n = ARRAY_SIZE(uml_regsets)
};

const struct user_regset_view *
task_user_regset_view(struct task_struct *tsk)
{
	return &user_uml_view;
}

/* read/write a word at location addr in the USER area (guest ptrace) */
int peek_user(struct task_struct *child, long addr, long data)
{
	unsigned long tmp;

	if ((addr & 7) || addr < 0)
		return -EIO;

	tmp = 0;  /* Default return condition */
	if (addr < MAX_REG_OFFSET)
		tmp = getreg(child, addr);
	return put_user(tmp, (unsigned long *) data);
}

int poke_user(struct task_struct *child, long addr, long data)
{
	if ((addr & 7) || addr < 0)
		return -EIO;

	if (addr < MAX_REG_OFFSET)
		return putreg(child, addr, data);
	return -EIO;
}

/*
 * Backend ptrace dispatch: everything not covered by the generic
 * regset path (which flows through task_user_regset_view). TLS and
 * debug-register requests land with their own surfaces later.
 */
long subarch_ptrace(struct task_struct *child, long request,
		    unsigned long addr, unsigned long data)
{
	return -EIO;
}
