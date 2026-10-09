// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>

#include <shared/irq_kern.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <as-layout.h>
#include <os.h>
#include <skas.h>
#include <stub-data.h>

/* Ensure the stub_data struct covers the allocated area */
static_assert(sizeof(struct stub_data) == STUB_DATA_PAGES * UM_KERN_PAGE_SIZE);

static spinlock_t mm_list_lock;
static struct list_head mm_list;

struct mutex *__get_turnstile(struct mm_id *mm_id)
{
	struct mm_context *ctx = container_of(mm_id, struct mm_context, id);

	return &ctx->turnstile;
}

void enter_turnstile(struct mm_id *mm_id)
{
	mutex_lock(__get_turnstile(mm_id));
}

void exit_turnstile(struct mm_id *mm_id)
{
	mutex_unlock(__get_turnstile(mm_id));
}

/* The stub thread that runs (or is about to run) userspace on this CPU */
static DEFINE_PER_CPU(struct stub_thread *, running_stub);

/*
 * Make the userspace code running on this CPU (if any) trap back into the
 * kernel. A stub thread still in its signal handler gets the signal as soon
 * as it returns to userspace, so nothing is lost if this races with
 * entering userspace.
 */
void um_kick_userspace(void)
{
	struct stub_thread *st = __this_cpu_read(running_stub);

	if (st)
		stub_thread_kick(st);
}

/**
 * enter_userspace() - prepare to run userspace of @mm_id on this CPU
 * @mm_id: The stub process
 *
 * Picks the stub thread to run on, and replays pending page table changes
 * by queueing them to that thread. They are executed when the thread is
 * woken up to run userspace, so the CPU must not be preempted in between:
 * another task could change the page tables meanwhile, and the stale queued
 * requests would then be applied after its TLB flush.
 *
 * Return: the stub thread to run on. Preemption is disabled until
 * leave_userspace(), and the thread is the target of um_kick_userspace().
 */
struct stub_thread *enter_userspace(struct mm_id *mm_id)
{
	struct mm_context *ctx = container_of(mm_id, struct mm_context, id);
	struct mm_struct *mm = container_of(ctx, struct mm_struct, context);
	struct stub_thread *st = &mm_id->main;

	/*
	 * At any given time, only one CPU thread can enter the turnstile to
	 * operate on the same stub process, including executing stub system
	 * calls (mmap and munmap).
	 */
	enter_turnstile(mm_id);

	preempt_disable();

	um_tlb_sync(mm, st);

	/* Not irq-safe this_cpu_write(), that costs host syscalls on SMP */
	__this_cpu_write(running_stub, st);
	/* An IPI handler on this CPU sees it from here on */
	barrier();

	/*
	 * Work that arrived since interrupt_end() did not kick us, as
	 * running_stub was not set yet; make the stub trap right away.
	 */
	if (need_resched() || test_thread_flag(TIF_SIGPENDING) ||
	    test_thread_flag(TIF_NOTIFY_SIGNAL) ||
	    test_thread_flag(TIF_NOTIFY_RESUME))
		stub_thread_kick(st);

	return st;
}

void leave_userspace(struct mm_id *mm_id, struct stub_thread *st)
{
	__this_cpu_write(running_stub, NULL);
	preempt_enable();
	exit_turnstile(mm_id);
}

unsigned long current_stub_stack(void)
{
	if (current->mm == NULL)
		return 0;

	return current->mm->context.id.main.stack;
}

struct mm_id *current_mm_id(void)
{
	if (current->mm == NULL)
		return NULL;

	return &current->mm->context.id;
}

int init_new_context(struct task_struct *task, struct mm_struct *mm)
{
	struct mm_id *new_id = &mm->context.id;
	unsigned long stack = 0;
	int ret = -ENOMEM;

	mutex_init(&mm->context.turnstile);
	spin_lock_init(&mm->context.sync_tlb_lock);

	stack = __get_free_pages(GFP_KERNEL | __GFP_ZERO,
				 get_order(STUB_DATA_PAGES * UM_KERN_PAGE_SIZE));
	if (stack == 0)
		goto out;

	memset(new_id, 0, sizeof(*new_id));
	new_id->main.stack = stack;

	scoped_guard(spinlock_irqsave, &mm_list_lock) {
		/* Insert into list, used for lookups when the child dies */
		list_add(&mm->context.list, &mm_list);
	}

	ret = start_userspace(new_id);
	if (ret < 0)
		goto out_free;

	/* Ensure the new MM is clean and nothing unwanted is mapped */
	unmap(&new_id->main, 0, STUB_START);

	return 0;

 out_free:
	scoped_guard(spinlock_irqsave, &mm_list_lock)
		list_del(&mm->context.list);
	free_pages(new_id->main.stack, get_order(STUB_DATA_PAGES * UM_KERN_PAGE_SIZE));
 out:
	return ret;
}

void destroy_context(struct mm_struct *mm)
{
	struct mm_context *mmu = &mm->context;

	/*
	 * If init_new_context wasn't called, this will be
	 * zero, resulting in a kill(0), which will result in the
	 * whole UML suddenly dying.  Also, cover negative and
	 * 1 cases, since they shouldn't happen either.
	 *
	 * Negative cases happen if the child died unexpectedly.
	 */
	if (mmu->id.pid >= 0 && mmu->id.pid < 2) {
		printk(KERN_ERR "corrupt mm_context - pid = %d\n",
		       mmu->id.pid);
		return;
	}

	scoped_guard(spinlock_irqsave, &mm_list_lock)
		list_del(&mm->context.list);

	if (mmu->id.pid > 0) {
		os_kill_ptraced_process(mmu->id.pid, 1);
		mmu->id.pid = -1;
	}

	if (using_seccomp && mmu->id.sock)
		os_close_file(mmu->id.sock);

	free_pages(mmu->id.main.stack, get_order(STUB_DATA_PAGES * UM_KERN_PAGE_SIZE));
}

static irqreturn_t mm_sigchld_irq(int irq, void* dev)
{
	struct mm_context *mm_context;
	pid_t pid;

	guard(spinlock)(&mm_list_lock);

	while ((pid = os_reap_child()) > 0) {
		/*
		* A child died, check if we have an MM with the PID. This is
		* only relevant in SECCOMP mode (as ptrace will fail anyway).
		*
		* See wait_stub_done_seccomp for more details.
		*/
		list_for_each_entry(mm_context, &mm_list, list) {
			if (mm_context->id.pid == pid) {
				struct stub_data *stub_data;
				printk("Unexpectedly lost MM child! Affected tasks will segfault.");

				/* Marks the MM as dead */
				WRITE_ONCE(mm_context->id.pid, -1);

				stub_data = (void *)mm_context->id.main.stack;
				/* A waiter that sees this also sees the pid */
				smp_store_release(&stub_data->futex,
						  FUTEX_IN_KERN);
#if IS_ENABLED(CONFIG_SMP)
				os_futex_wake(&stub_data->futex);
#else
				/*
				 * On !SMP the wake is skipped: a futex
				 * waiter in wait_stub_done_seccomp() is
				 * rescued by a signal interrupting its
				 * wait (EINTR) or by the bounded-wait
				 * liveness probe there (pidfd poll on
				 * timeout), which turns a dead stub
				 * into a loud failure either way.
				 */
#endif

				/*
				 * NOTE: Currently executing syscalls by
				 * affected tasks may finish normally.
				 */
				break;
			}
		}
	}

	return IRQ_HANDLED;
}

static int __init init_child_tracking(void)
{
	int err;

	spin_lock_init(&mm_list_lock);
	INIT_LIST_HEAD(&mm_list);

	err = request_irq(SIGCHLD_IRQ, mm_sigchld_irq, 0, "SIGCHLD", NULL);
	if (err < 0)
		panic("Failed to register SIGCHLD IRQ: %d", err);

	return 0;
}
early_initcall(init_child_tracking)
