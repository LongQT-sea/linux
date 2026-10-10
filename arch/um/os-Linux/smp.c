// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Ant Group
 * Author: Tiwei Bie <tiwei.btw@antgroup.com>
 */

#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <kern_util.h>
#include <init.h>
#include <os.h>
#include <smp.h>
#include "internal.h"

/*
 * vcpu_affinity=: the host CPU each virtual CPU runs on. A virtual CPU is two
 * kinds of host threads: its own, and the stub threads that run userspace for
 * it. They hand over to each other on every system call, fault and tick, which
 * is cheap on one host CPU and costly across CPUs, so both go on the same one.
 * Other host threads and processes (stub processes, helpers) may run on any
 * of the listed CPUs.
 */
static int vcpu_host_cpu[CONFIG_NR_CPUS];
static int vcpu_host_cpus;
static cpu_set_t vcpu_host_set;

static int __init vcpu_affinity_setup(char *line, int *add)
{
	char *p = line, *end;
	long c;

	*add = 0;
	CPU_ZERO(&vcpu_host_set);
	vcpu_host_cpus = 0;
	while (vcpu_host_cpus < CONFIG_NR_CPUS) {
		c = strtol(p, &end, 10);
		if (end == p || c < 0 || c >= CPU_SETSIZE) {
			os_warn("vcpu_affinity=: bad host CPU list '%s', ignored\n",
				line);
			vcpu_host_cpus = 0;
			return 0;
		}
		vcpu_host_cpu[vcpu_host_cpus++] = c;
		CPU_SET(c, &vcpu_host_set);
		if (*end != ',')
			break;
		p = end + 1;
	}
	return 0;
}

__uml_setup("vcpu_affinity=", vcpu_affinity_setup,
"vcpu_affinity=<host cpu>,<host cpu>,...\n"
"    Run virtual CPU 0 on the first host CPU listed, CPU 1 on the second, and\n"
"    so on, together with the stub threads that run userspace for them.\n"
"    Virtual CPUs beyond the list, and everything else, run on any of the\n"
"    listed host CPUs.\n\n"
);

static void set_affinity(int tid, int cpu)
{
	static bool warned;
	cpu_set_t set;

	if (!vcpu_host_cpus)
		return;
	if (cpu >= 0 && cpu < vcpu_host_cpus) {
		CPU_ZERO(&set);
		CPU_SET(vcpu_host_cpu[cpu], &set);
	} else {
		set = vcpu_host_set;
	}
	/* The raw call: the kernel has a sched_setaffinity() of its own */
	if (syscall(__NR_sched_setaffinity, tid, sizeof(set), &set) < 0 &&
	    !warned) {
		warned = true;
		os_warn("vcpu_affinity: cannot set the affinity of thread %d, errno = %d (host CPU not allowed?)\n",
			tid ? tid : (int)syscall(__NR_gettid), errno);
	}
}

/*
 * Pin the stub thread @tid of stub process @pid that runs userspace for CPU
 * @cpu. The thread reported its tid through stub data, which guest code in
 * other threads of the process can write: only pin a thread of that process.
 */
void os_pin_stub_thread(int pid, int tid, int cpu)
{
	char path[64];

	if (!vcpu_host_cpus)
		return;
	snprintf(path, sizeof(path), "/proc/%d/task/%d", pid, tid);
	if (access(path, F_OK))
		return;
	set_affinity(tid, cpu);
}

/*
 * Host threads and processes inherit the affinity of the CPU thread that
 * creates them: let this one run anywhere while it creates something that is
 * not tied to it, and come back afterwards.
 */
void os_affinity_widen(void)
{
	set_affinity(0, -1);
}

void os_affinity_restore(void)
{
	set_affinity(0, uml_curr_cpu());
}

struct cpu_thread_data {
	int cpu;
	sigset_t sigset;
};

static __thread int __curr_cpu;

int uml_curr_cpu(void)
{
	return __curr_cpu;
}

static pthread_t cpu_threads[CONFIG_NR_CPUS];

/*
 * Static rather than kmalloc()ed: the data is released on the host thread's
 * own stack, where KASAN's free-stack recording (dump_trace() walks up to a
 * THREAD_SIZE boundary) would run off the end of the pthread stack.
 */
static struct cpu_thread_data cpu_thread_data[CONFIG_NR_CPUS];

static void *cpu_thread(void *arg)
{
	struct cpu_thread_data *data = arg;

	__curr_cpu = data->cpu;
	set_affinity(0, data->cpu);

	uml_start_secondary(data);

	return NULL;
}

int os_start_cpu_thread(int cpu)
{
	struct cpu_thread_data *data = &cpu_thread_data[cpu];
	sigset_t sigset, oset;
	int err;

	sigfillset(&sigset);
	if (sigprocmask(SIG_SETMASK, &sigset, &oset) < 0)
		return -errno;

	data->cpu = cpu;
	data->sigset = oset;

	err = pthread_create(&cpu_threads[cpu], NULL, cpu_thread, data);
	if (sigprocmask(SIG_SETMASK, &oset, NULL) < 0)
		panic("Failed to restore the signal mask, errno = %d", errno);
	if (err != 0)
		return -err;

	return 0;
}

void os_start_secondary(void *arg, jmp_buf *switch_buf)
{
	struct cpu_thread_data *data = arg;

	/*
	 * IPIs stay unblocked: until this CPU enables interrupts (its
	 * signals_enabled starts at 0), the handler only records them.
	 */
	sigaddset(&data->sigset, SIGIO);

	if (sigprocmask(SIG_SETMASK, &data->sigset, NULL) < 0)
		panic("Failed to restore the signal mask, errno = %d", errno);

	longjmp(*switch_buf, 1);

	/* unreachable */
	printk(UM_KERN_ERR "impossible long jump!");
	fatal_sigsegv();
}

int os_send_ipi(int cpu, int vector)
{
	union sigval value = { .sival_int = vector };

	return pthread_sigqueue(cpu_threads[cpu], IPI_SIGNAL, value);
}

/* IPI vectors that arrived on this CPU while its signals were disabled */
static __thread unsigned int ipis_pending;

/*
 * IPIs are deferred in software while signals are disabled, like the other
 * signals the kernel handles as interrupts (see os-Linux/signal.c), instead
 * of blocking the signal with sigprocmask() on every interrupt disable and
 * enable, which costs two host syscalls each time. Vectors are idempotent
 * (the work they stand for is queued elsewhere), so a bitmask is enough.
 */
static void ipi_sig_handler(int sig, siginfo_t *si, void *uc)
{
	int save_errno = errno;

	if (!signals_enabled) {
		ipis_pending |= 1U << si->si_value.sival_int;
		os_mark_ipi_pending();
		errno = save_errno;
		return;
	}

	signals_enabled = 0;
	um_trace_signals_off();

	uml_ipi_handler(si->si_value.sival_int);

	um_trace_signals_on();
	signals_enabled = 1;

	errno = save_errno;
}

/* Only called while signals are enabled: the handler does not defer then */
unsigned int os_take_pending_ipis(void)
{
	unsigned int ipis = ipis_pending;

	ipis_pending = 0;
	return ipis;
}

/* Run deferred IPIs, with signals disabled */
void os_run_ipis(unsigned int vectors)
{
	while (vectors) {
		int vector = __builtin_ctz(vectors);

		vectors &= vectors - 1;
		uml_ipi_handler(vector);
	}
}

void __init os_init_smp(void)
{
	struct sigaction action = {
		.sa_sigaction = ipi_sig_handler,
		.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART,
	};

	sigfillset(&action.sa_mask);

	if (sigaction(IPI_SIGNAL, &action, NULL) < 0)
		panic("%s: sigaction failed, errno = %d", __func__, errno);

	cpu_threads[0] = pthread_self();
	set_affinity(0, 0);
}
