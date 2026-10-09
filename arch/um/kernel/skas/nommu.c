// SPDX-License-Identifier: GPL-2.0
#include <linux/cpumask.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/mutex.h>
#include <linux/panic.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <as-layout.h>
#include <mem.h>
#include <os.h>
#include <skas.h>
#include <mm_id.h>

struct nommu_runner {
	struct mm_id mm_id;
	struct mutex turnstile;
};

static struct nommu_runner nommu_runners[NR_CPUS];

static struct nommu_runner *this_runner(void)
{
	return &nommu_runners[raw_smp_processor_id()];
}

struct mutex *__get_turnstile(struct mm_id *mm_id)
{
	return &container_of(mm_id, struct nommu_runner, mm_id)->turnstile;
}

void enter_turnstile(struct mm_id *mm_id)
{
	mutex_lock(__get_turnstile(mm_id));
}

void exit_turnstile(struct mm_id *mm_id)
{
	mutex_unlock(__get_turnstile(mm_id));
}

static DEFINE_PER_CPU(struct stub_thread *, running_stub);

struct stub_thread *enter_userspace(struct mm_id *mm_id)
{
	enter_turnstile(mm_id);
	preempt_disable();
	__this_cpu_write(running_stub, &mm_id->main);

	return &mm_id->main;
}

void leave_userspace(struct mm_id *mm_id, struct stub_thread *st)
{
	__this_cpu_write(running_stub, NULL);
	preempt_enable();
	exit_turnstile(mm_id);
}

void userspace_failed(struct mm_id *mm_id, struct stub_thread *st)
{
	st->syscall_data_len = 0;
	st->syscall_fd_num = 0;
}

void um_kick_userspace(void)
{
	struct stub_thread *st = __this_cpu_read(running_stub);

	if (st)
		stub_thread_kick(st);
}

unsigned long current_stub_stack(void)
{
	return this_runner()->mm_id.main.stack;
}

struct mm_id *current_mm_id(void)
{
	return &this_runner()->mm_id;
}

static int __init nommu_start_runners(void)
{
	unsigned long long offset;
	struct nommu_runner *r;
	int cpu, err, fd;

	fd = phys_mapping(uml_reserved - uml_physmem, &offset);

	for_each_possible_cpu(cpu) {
		r = &nommu_runners[cpu];
		mutex_init(&r->turnstile);

		r->mm_id.main.stack = __get_free_pages(GFP_KERNEL | __GFP_ZERO,
						       ilog2(STUB_DATA_PAGES));
		if (!r->mm_id.main.stack)
			panic("OOM allocating userspace stack for CPU %d", cpu);

		err = start_userspace(&r->mm_id);
		if (err < 0)
			panic("userspace startup for CPU %d failed: %d",
			      cpu, err);

		map(&r->mm_id.main, uml_reserved, high_physmem - uml_reserved,
		    UM_PROT_READ | UM_PROT_WRITE | UM_PROT_EXEC, fd, offset);

		err = syscall_stub_flush(&r->mm_id.main);
		if (err < 0)
			panic("physmem map/flush failed for CPU%d: %d",
			      cpu, err);
	}

	return 0;
}
arch_initcall(nommu_start_runners);
