/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_X86_PTRACE_H
#define __UM_X86_PTRACE_H

/* This is here because signal.c needs the REGSET_FP_LEGACY definition */
enum {
	REGSET_GENERAL,
#ifdef CONFIG_X86_32
	REGSET_FP_LEGACY,
#endif
	REGSET_FP,
	REGSET_XSTATE,
};

#include <linux/compiler.h>
#include <asm/thread_info.h>	/* THREAD_SIZE for the stack-access API */
#ifndef CONFIG_X86_32
#define __FRAME_OFFSETS /* Needed to get the R* macros */
#endif
#include <asm/ptrace-generic.h>

#define user_mode(r) UPT_IS_USER(&(r)->regs)

#define PT_REGS_AX(r) UPT_AX(&(r)->regs)
#define PT_REGS_BX(r) UPT_BX(&(r)->regs)
#define PT_REGS_CX(r) UPT_CX(&(r)->regs)
#define PT_REGS_DX(r) UPT_DX(&(r)->regs)

#define PT_REGS_SI(r) UPT_SI(&(r)->regs)
#define PT_REGS_DI(r) UPT_DI(&(r)->regs)
#define PT_REGS_BP(r) UPT_BP(&(r)->regs)
#define PT_REGS_EFLAGS(r) UPT_EFLAGS(&(r)->regs)

#define PT_REGS_CS(r) UPT_CS(&(r)->regs)
#define PT_REGS_SS(r) UPT_SS(&(r)->regs)
#define PT_REGS_DS(r) UPT_DS(&(r)->regs)
#define PT_REGS_ES(r) UPT_ES(&(r)->regs)

#define PT_REGS_ORIG_SYSCALL(r) PT_REGS_AX(r)
#define PT_REGS_SYSCALL_RET(r) PT_REGS_AX(r)

#define PT_FIX_EXEC_STACK(sp) do ; while(0)

#define profile_pc(regs) PT_REGS_IP(regs)

#define UPT_RESTART_SYSCALL(r) (UPT_IP(r) -= 2)
#define PT_REGS_SET_SYSCALL_RETURN(r, res) (PT_REGS_AX(r) = (res))

static inline long regs_return_value(struct pt_regs *regs)
{
	return PT_REGS_AX(regs);
}

/*
 * Forward declaration to avoid including sysdep/tls.h, which causes a
 * circular include, and compilation failures.
 */
struct user_desc;

#ifdef CONFIG_X86_32

extern int ptrace_get_thread_area(struct task_struct *child, int idx,
                                  struct user_desc __user *user_desc);

extern int ptrace_set_thread_area(struct task_struct *child, int idx,
                                  struct user_desc __user *user_desc);

extern int arch_switch_tls(struct task_struct *to);

#else

#define PT_REGS_R8(r) UPT_R8(&(r)->regs)
#define PT_REGS_R9(r) UPT_R9(&(r)->regs)
#define PT_REGS_R10(r) UPT_R10(&(r)->regs)
#define PT_REGS_R11(r) UPT_R11(&(r)->regs)
#define PT_REGS_R12(r) UPT_R12(&(r)->regs)
#define PT_REGS_R13(r) UPT_R13(&(r)->regs)
#define PT_REGS_R14(r) UPT_R14(&(r)->regs)
#define PT_REGS_R15(r) UPT_R15(&(r)->regs)

#include <asm/errno.h>

static inline int ptrace_get_thread_area(struct task_struct *child, int idx,
                                         struct user_desc __user *user_desc)
{
        return -ENOSYS;
}

static inline int ptrace_set_thread_area(struct task_struct *child, int idx,
                                         struct user_desc __user *user_desc)
{
        return -ENOSYS;
}

extern long arch_prctl(struct task_struct *task, int option,
		       unsigned long __user *addr);

#endif

#define user_stack_pointer(regs) PT_REGS_SP(regs)

#define instruction_pointer_set(regs, val) (PT_REGS_IP(regs) = (val))

/*
 * The regs-and-stack access API (KPROBE_EVENTS fetches probe arguments
 * through it). um's kernel stack is in-process host memory; PT_REGS_SP
 * is the interrupted stack. Same shape as x86's header inlines, on the
 * um accessors.
 */
#define kernel_stack_pointer(regs)	(PT_REGS_SP(regs))

static inline bool regs_within_kernel_stack(struct pt_regs *regs,
					    unsigned long addr)
{
	return ((addr & ~(THREAD_SIZE - 1)) ==
		(PT_REGS_SP(regs) & ~(THREAD_SIZE - 1)));
}

static inline unsigned long *regs_get_kernel_stack_nth_addr(struct pt_regs *regs,
							    unsigned int n)
{
	unsigned long *addr = (unsigned long *)PT_REGS_SP(regs);

	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return addr;
	else
		return NULL;
}

/* To avoid include hell, we can't include uaccess.h */
extern long copy_from_kernel_nofault(void *dst, const void *src, size_t size);

static inline unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs,
						      unsigned int n)
{
	unsigned long *addr;
	unsigned long val;
	long ret;

	addr = regs_get_kernel_stack_nth_addr(regs, n);
	if (addr) {
		ret = copy_from_kernel_nofault(&val, addr, sizeof(val));
		if (!ret)
			return val;
	}
	return 0;
}

/*
 * regs_get_register() - get register value from its offset
 * @regs:	pt_regs from which register value is gotten
 * @offset:	offset of the register in pt_regs (i.e. in the uml_pt_regs
 *		gp[] array, see regs_query_register_offset())
 *
 * If @offset is out of the gp[] register frame, this returns 0.
 */
static inline unsigned long regs_get_register(struct pt_regs *regs,
					      unsigned int offset)
{
	if (unlikely(offset >= MAX_REG_OFFSET))
		return 0;
	return *(unsigned long *)((unsigned long)regs + offset);
}

extern int regs_query_register_offset(const char *name);

extern void arch_switch_to(struct task_struct *to);

#endif /* __UM_X86_PTRACE_H */
