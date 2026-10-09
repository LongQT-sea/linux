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

/*
 * Like um_kick_userspace(), and wait until the userspace code running on this
 * CPU (if any) is back in the kernel: all of its memory accesses until then
 * are visible to this CPU, and it passed a context synchronisation (the trap).
 */
void um_stop_userspace(void)
{
	struct stub_thread *st = __this_cpu_read(running_stub);

	if (!st)
		return;

	if (using_seccomp)
		stub_thread_stop(st);
	else
		stub_thread_kick(st);
}

static void stub_thread_dead(struct stub_thread *st)
{
	struct stub_data *stub_data = (void *)st->stack;

	/* A waiter that sees this also sees that the stub process is dead */
	smp_store_release(&stub_data->futex, FUTEX_IN_KERN);
#if IS_ENABLED(CONFIG_SMP)
	os_futex_wake(&stub_data->futex);
#else
	/*
	 * On !SMP the wake is skipped: a futex waiter in stub_wait() is
	 * rescued by a signal interrupting its wait (EINTR) or by its
	 * periodic liveness check, which turns a dead stub into a loud
	 * failure either way.
	 */
#endif
}

/*
 * Kill the stub process and wait for it to be gone, and wake up anybody
 * waiting for one of its threads. Its tasks die when they next use it.
 */
void um_stub_kill(struct mm_id *mm_id)
{
	int __maybe_unused cpu;
	int pid;

	/* This returns once the process is gone, also if another CPU killed it */
	pid = stub_kill(mm_id);
	if (!pid)
		return;

	pr_err("Killed stub process %d, its host mappings were inconsistent\n",
	       pid);

	stub_thread_dead(&mm_id->main);
#ifdef CONFIG_UML_MT_STUB
	for (cpu = 0; cpu < ARRAY_SIZE(mm_id->workers); cpu++) {
		struct stub_thread *st = READ_ONCE(mm_id->workers[cpu]);

		if (st)
			stub_thread_dead(st);
	}
#endif
}

static void free_stub_thread_data(struct stub_thread *st)
{
	free_pages(st->stack, get_order(STUB_DATA_PAGES * UM_KERN_PAGE_SIZE));
}

#ifdef CONFIG_UML_MT_STUB
static bool mt_stub __ro_after_init;

static int __init init_mt_stub(void)
{
	mt_stub = using_seccomp && num_possible_cpus() > 1;
	return 0;
}
early_initcall(init_mt_stub);

static bool use_mt_stub(void)
{
	return mt_stub;
}

static struct stub_thread *alloc_stub_thread(struct mm_id *mm_id, int cpu)
{
	struct stub_thread *st;

	st = kzalloc_obj(*st, GFP_KERNEL);
	if (!st)
		return NULL;

	st->stack = __get_free_pages(GFP_KERNEL | __GFP_ZERO,
				     get_order(STUB_DATA_PAGES * UM_KERN_PAGE_SIZE));
	if (!st->stack) {
		kfree(st);
		return NULL;
	}

	st->mm = mm_id;
	st->slot = 1 + cpu;

	return st;
}

static void free_stub_thread(struct stub_thread *st)
{
	free_stub_thread_data(st);
	kfree(st);
}

/*
 * Return the stub thread that runs userspace of @mm_id on this CPU, creating
 * it if needed. Called and returns with preemption disabled, but may enable
 * it in between, so the CPU can change.
 */
static struct stub_thread *get_cpu_stub_thread(struct mm_id *mm_id)
{
	struct mm_context *ctx = container_of(mm_id, struct mm_context, id);
	struct stub_thread *st, *new = NULL;
	int cpu, err;

	for (;;) {
		cpu = smp_processor_id();
		st = READ_ONCE(mm_id->workers[cpu]);
		if (st)
			break;

		/* Only this CPU creates its own thread, preemption is off */
		if (new && new->slot == 1 + cpu) {
			/*
			 * The thread that exec()ed the stub creates the new
			 * one; it is shared by all CPUs.
			 */
			scoped_guard(spinlock, &ctx->sync_tlb_lock) {
				err = start_stub_thread(new);
				/*
				 * The data pages may be mapped in the stub,
				 * so it must go before they are freed.
				 */
				if (err)
					um_stub_kill(mm_id);
			}
			if (err) {
				pr_err("%s: failed to start stub thread for CPU %d: %d\n",
				       __func__, cpu, err);
				free_stub_thread(new);
				return NULL;
			}
			/* Paired with READ_ONCE() of others walking workers[] */
			smp_store_release(&mm_id->workers[cpu], new);
			return new;
		}

		preempt_enable();
		if (new)
			free_stub_thread(new);
		new = alloc_stub_thread(mm_id, cpu);
		preempt_disable();
		if (!new)
			return NULL;
	}

	if (new)
		free_stub_thread(new);

	return st;
}
#else
static bool use_mt_stub(void)
{
	return false;
}

static struct stub_thread *get_cpu_stub_thread(struct mm_id *mm_id)
{
	return NULL;
}
#endif

bool um_mm_running_elsewhere(struct mm_struct *mm)
{
	return use_mt_stub() && !cpumask_empty(mm_cpumask(mm));
}

/**
 * enter_userspace() - prepare to run userspace of @mm_id on this CPU
 * @mm_id: The stub process
 *
 * Picks the stub thread to run on, and replays pending page table changes
 * by queueing them to that thread.
 *
 * With worker threads (CONFIG_UML_MT_STUB), the queued changes are only
 * executed when the thread is woken up to run userspace. Until they are
 * done, a TLB flush on another CPU must not update the host mappings (it
 * could be undone by a stale queued mmap), so sync_tlb_lock stays held
 * until userspace_running(). This CPU is in mm_cpumask() from here until
 * leave_userspace(), so flushes on other CPUs update the host right away.
 *
 * Return: the stub thread to run on, or NULL if that failed (the caller must
 * then kill the task). On success, preemption is disabled until
 * leave_userspace(), and the thread is the target of um_kick_userspace().
 */
struct stub_thread *enter_userspace(struct mm_id *mm_id)
{
	struct mm_context *ctx = container_of(mm_id, struct mm_context, id);
	struct mm_struct *mm = container_of(ctx, struct mm_struct, context);
	struct stub_thread *st;

	/*
	 * Without worker threads, only one CPU thread at a time can enter
	 * the turnstile to operate on the same stub process, including
	 * executing stub system calls (mmap and munmap). Worker threads are
	 * per CPU, and what they share is protected by sync_tlb_lock.
	 */
	if (!use_mt_stub())
		enter_turnstile(mm_id);

	preempt_disable();

	if (!use_mt_stub()) {
		st = &mm_id->main;
		um_tlb_sync(mm, st);
	} else {
		st = get_cpu_stub_thread(mm_id);
		if (!st)
			goto fail;

		spin_lock(&ctx->sync_tlb_lock);
		st->sync_locked = 1;
		um_tlb_sync_locked(mm, st);
		cpumask_set_cpu(smp_processor_id(), mm_cpumask(mm));
	}

	/* A failed flush of queued syscalls leaves the host out of sync */
	if (st->syscall_data_len < 0) {
		userspace_failed(mm_id, st);
		leave_userspace(mm_id, st);
		return NULL;
	}

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

fail:
	preempt_enable();
	return NULL;
}

/*
 * Running userspace failed after enter_userspace(). Requests queued to @st
 * may not have run although the page tables are marked as synced, so the
 * stub process cannot be trusted anymore (with worker threads, other threads
 * of it may still run userspace).
 */
void userspace_failed(struct mm_id *mm_id, struct stub_thread *st)
{
	if (use_mt_stub() || st->syscall_data_len)
		um_stub_kill(mm_id);

	st->syscall_data_len = 0;
	st->syscall_fd_num = 0;
}

/*
 * Called once the stub thread of enter_userspace() executed its queued
 * syscalls (or has none) and so runs userspace.
 */
void userspace_running(struct mm_id *mm_id, struct stub_thread *st)
{
	struct mm_context *ctx = container_of(mm_id, struct mm_context, id);

	if (st->sync_locked) {
		st->sync_locked = 0;
		spin_unlock(&ctx->sync_tlb_lock);
	}
}

void leave_userspace(struct mm_id *mm_id, struct stub_thread *st)
{
	struct mm_context *ctx = container_of(mm_id, struct mm_context, id);
	struct mm_struct *mm = container_of(ctx, struct mm_struct, context);

	__this_cpu_write(running_stub, NULL);
	userspace_running(mm_id, st);
	if (use_mt_stub())
		cpumask_clear_cpu(smp_processor_id(), mm_cpumask(mm));
	preempt_enable();
	if (!use_mt_stub())
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

	/*
	 * Ensure the new MM is clean and nothing unwanted is mapped. With
	 * worker threads this is done before the first one is created.
	 */
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
	int __maybe_unused cpu;

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

	free_stub_thread_data(&mmu->id.main);
#ifdef CONFIG_UML_MT_STUB
	for (cpu = 0; cpu < ARRAY_SIZE(mmu->id.workers); cpu++) {
		if (mmu->id.workers[cpu])
			free_stub_thread(mmu->id.workers[cpu]);
	}
#endif
}

static irqreturn_t mm_sigchld_irq(int irq, void* dev)
{
	struct mm_context *mm_context;
	int __maybe_unused cpu;
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
				printk("Unexpectedly lost MM child! Affected tasks will segfault.");

				/* Marks the MM as dead */
				WRITE_ONCE(mm_context->id.pid, -1);

				stub_thread_dead(&mm_context->id.main);
#ifdef CONFIG_UML_MT_STUB
				for (cpu = 0; cpu < ARRAY_SIZE(mm_context->id.workers); cpu++) {
					struct stub_thread *st;

					st = READ_ONCE(mm_context->id.workers[cpu]);
					if (st)
						stub_thread_dead(st);
				}
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
