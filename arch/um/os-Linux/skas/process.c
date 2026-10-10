// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Benjamin Berg <benjamin@sipsolutions.net>
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2002- 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <mem_user.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <asm/unistd.h>
#include <as-layout.h>
#include <init.h>
#include <kern_util.h>
#include <mem.h>
#include <os.h>
#include <ptrace_user.h>
#include <registers.h>
#include <skas.h>
#include <sysdep/stub.h>
#include <sysdep/mcontext.h>
#include <linux/futex.h>
#include <linux/threads.h>
#include <timetravel.h>
#include <asm-generic/rwonce.h>
#include "../internal.h"

int is_skas_winch(int pid, int fd, void *data)
{
	return pid == getpgrp();
}

static int ptrace_dump_regs(int pid)
{
	unsigned long regs[UM_GP_SLOTS];
	int i, err;

	err = ptrace_getregs(pid, regs);
	if (err < 0)
		return err;

	printk(UM_KERN_ERR "Stub registers -\n");
	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		const char *regname = ptrace_reg_name(i);

		printk(UM_KERN_ERR "\t%s\t(%2d): %lx\n", regname, i, regs[i]);
	}

	return 0;
}

/*
 * Signals that are OK to receive in the stub - we'll just continue it.
 * SIGWINCH will happen when UML is inside a detached screen.
 */
#define STUB_SIG_MASK ((1 << SIGALRM) | (1 << SIGWINCH))

/*
 * Constant form of "does this architecture hide a register at a
 * syscall stop", usable inside an if condition.
 */
#ifdef UM_SYSCALL_STOP_HIDES_REG
#define UM_SYSCALL_STOP_HIDDEN 1
#else
#define UM_SYSCALL_STOP_HIDDEN 0
#endif

/* Signals that the stub will finish with - anything else is an error */
#define STUB_DONE_MASK (1 << SIGTRAP)

void wait_stub_done(int pid)
{
	int n, status, err;

	while (1) {
		CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED | __WALL));
		if ((n < 0) || !WIFSTOPPED(status))
			goto bad_wait;

		if (((1 << WSTOPSIG(status)) & STUB_SIG_MASK) == 0)
			break;

		err = ptrace(PTRACE_CONT, pid, 0, 0);
		if (err) {
			printk(UM_KERN_ERR "%s : continue failed, errno = %d\n",
			       __func__, errno);
			fatal_sigsegv();
		}
	}

	if (((1 << WSTOPSIG(status)) & STUB_DONE_MASK) != 0)
		return;

bad_wait:
	err = ptrace_dump_regs(pid);
	if (err)
		printk(UM_KERN_ERR "Failed to get registers from stub, errno = %d\n",
		       -err);
	printk(UM_KERN_ERR "%s : failed to wait for SIGTRAP, pid = %d, n = %d, errno = %d, status = 0x%x\n",
	       __func__, pid, n, errno, status);
	fatal_sigsegv();
}

/* Whether the stub process @pid exited (or was already reaped) */
static bool stub_process_dead(int pid)
{
	siginfo_t info = {};

	/* 0: another CPU is killing it, see stub_kill() */
	if (pid <= 0)
		return pid < 0;

	if (waitid(P_PID, pid, &info, WEXITED | WNOHANG | WNOWAIT | __WALL) < 0)
		return errno == ECHILD;

	return info.si_pid == pid;
}

/*
 * Kill the stub process of @mm_id and wait until it is gone, so that none of
 * its host mappings remain. Used when the host mappings can no longer be
 * trusted to match the page tables.
 *
 * Return: the pid of the process this call killed, or 0 if it was dead
 * already or killed by another CPU (it is gone when this returns either way).
 */
int stub_kill(struct mm_id *mm_id)
{
	int pid = __atomic_load_n(&mm_id->pid, __ATOMIC_ACQUIRE);

	/*
	 * Claim the process by setting the pid to 0 ("being killed"), so that
	 * only one CPU kills and reaps it: a second waitpid() could reap an
	 * unrelated process that got the pid meanwhile. The SIGCHLD IRQ
	 * handler no longer finds it by its pid; if it reaps it anyway,
	 * waitpid() below returns ECHILD.
	 */
	while (pid > 0 &&
	       !__atomic_compare_exchange_n(&mm_id->pid, &pid, 0, false,
					    __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
		;

	if (pid > 0) {
		kill(pid, SIGKILL);
		/*
		 * Wait until all of its threads are gone. Only then mark it
		 * dead: other CPUs take that to mean that none of its host
		 * mappings is left.
		 */
		CATCH_EINTR(waitpid(pid, NULL, __WALL));
		__atomic_store_n(&mm_id->pid, -1, __ATOMIC_RELEASE);
		return pid;
	}

	/* Another CPU is killing it: wait until it is gone */
	while (pid == 0) {
		sched_yield();
		pid = __atomic_load_n(&mm_id->pid, __ATOMIC_ACQUIRE);
	}

	return 0;
}

/* Pass the FDs for queued syscalls to @st and wake it up */
static void stub_wake(struct stub_thread *st)
{
	struct stub_data *data = (void *)st->stack;
	const char byte = 0;
	struct iovec iov = {
		.iov_base = (void *)&byte,
		.iov_len = sizeof(byte),
	};
	union {
		char data[CMSG_SPACE(sizeof(st->syscall_fd_map))];
		struct cmsghdr align;
	} ctrl;
	struct msghdr msgh = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};

	if (st->syscall_fd_num) {
		unsigned int fds_size = sizeof(int) * st->syscall_fd_num;
		struct cmsghdr *cmsg;

		msgh.msg_control = ctrl.data;
		msgh.msg_controllen = CMSG_SPACE(fds_size);
		cmsg = CMSG_FIRSTHDR(&msgh);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(fds_size);
		memcpy(CMSG_DATA(cmsg), st->syscall_fd_map, fds_size);

		CATCH_EINTR(syscall(__NR_sendmsg, st->mm->sock, &msgh, 0));
	}

	data->signal = 0;
	/* Paired with the acquire load in stub_signal_interrupt() */
	__atomic_store_n(&data->futex, FUTEX_IN_CHILD, __ATOMIC_RELEASE);
	CATCH_EINTR(syscall(__NR_futex, &data->futex,
			    FUTEX_WAKE, 1, NULL, NULL, 0));
}

/*
 * Waits for stub threads that only run stub code (to execute queued requests,
 * or to start) give up after this long: the caller may hold sync_tlb_lock,
 * with other CPUs spinning on it, and stub data is writable by userspace in
 * other threads of the stub process.
 */
#define STUB_WAIT_MAX_SECS 10

/*
 * A stub thread that is still runnable (or in uninterruptible sleep) after
 * STUB_WAIT_MAX_SECS gets more time, as the host may just be overloaded, but
 * not without end: guest code in another thread can redirect the stub thread
 * into a busy loop (stub data is writable), and the waiter may hold a lock
 * that other CPUs spin on.
 */
#define STUB_WAIT_LIMIT_SECS 60

/*
 * The host state of stub thread @tid of process @pid, as in
 * /proc/PID/task/TID/stat ('R' runnable, 'D' uninterruptible, 'S' sleeping,
 * ...), or 0 if it cannot be read.
 */
static char stub_thread_host_state(int pid, int tid)
{
	char path[64], buf[256];
	int fd, n, i;

	snprintf(path, sizeof(path), "/proc/%d/task/%d/stat", pid, tid);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = 0;
	/* "tid (comm) S ...": the comm may contain spaces and parentheses */
	for (i = n - 1; i >= 0 && buf[i] != ')'; i--)
		;
	return i >= 0 && i + 2 < n && buf[i + 1] == ' ' ? buf[i + 2] : 0;
}

/*
 * Wait until @st is back in its signal handler (@until_kern), or only until
 * it is no longer in the state it was woken up in (it may report that its
 * queued syscalls are done). Returns -1 if the stub process died, or if
 * @bounded and it took longer than STUB_WAIT_MAX_SECS.
 */
static int stub_wait(struct stub_thread *st, int until_kern, int bounded)
{
	struct stub_data *data = (void *)st->stack;
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 100000000 };
	struct timespec first, start, now;
	unsigned int val;
	int ret, pid;

	clock_gettime(CLOCK_MONOTONIC, &first);
	start = first;

	for (;;) {
		/*
		 * We need to check whether the child is still alive
		 * before and after the FUTEX_WAIT call. Before, in
		 * case it just died but we still updated data->futex
		 * to FUTEX_IN_CHILD. And after, in case it died while
		 * we were waiting (and SIGCHLD woke us up, see the
		 * IRQ handler in mmu.c).
		 *
		 * Either way, if PID is negative, then we have no
		 * choice but to kill the task.
		 */
		pid = __READ_ONCE(st->mm->pid);
		if (pid < 0)
			return -1;

		/*
		 * Paired with the release stores of the stub (and of
		 * stub_thread_dead()): what it wrote before is visible.
		 */
		val = __atomic_load_n(&data->futex, __ATOMIC_ACQUIRE);
		if (val == FUTEX_IN_KERN ||
		    (!until_kern && val != FUTEX_IN_CHILD))
			return 0;

		ret = syscall(__NR_futex, &data->futex, FUTEX_WAIT, val,
			      &timeout, NULL, 0);
		if (ret < 0 && errno != EINTR && errno != EAGAIN &&
		    errno != ETIMEDOUT) {
			printk(UM_KERN_ERR "%s : FUTEX_WAIT failed, errno = %d\n",
			       __func__, errno);
			return -1;
		}

		/*
		 * The SIGCHLD IRQ handler that notices a dead stub may not
		 * run for a while (another CPU may get the signal, interrupts
		 * may be disabled), so also check ourselves now and then.
		 */
		if (ret < 0 && errno != EAGAIN) {
			if (stub_process_dead(pid)) {
				printk(UM_KERN_ERR "%s : stub pid %d died during futex wait\n",
				       __func__, pid);
				return -1;
			}
			clock_gettime(CLOCK_MONOTONIC, &now);
			if (bounded &&
			    now.tv_sec - start.tv_sec > STUB_WAIT_MAX_SECS) {
				char state;

				/*
				 * This thread may not have run for a while
				 * itself (the host is busy): the stub may have
				 * answered since the futex wait timed out.
				 */
				val = __atomic_load_n(&data->futex, __ATOMIC_ACQUIRE);
				if (val == FUTEX_IN_KERN ||
				    (!until_kern && val != FUTEX_IN_CHILD))
					return 0;

				state = stub_thread_host_state(pid, st->tid);

				/*
				 * Only give up on a thread that sleeps (in its
				 * futex wait, as nothing else blocks in stub code
				 * for long): one that is runnable or in an
				 * uninterruptible wait is only slowed down by the
				 * host (CPU or memory pressure), and will answer.
				 */
				if ((state == 'R' || state == 'D') &&
				    now.tv_sec - first.tv_sec <= STUB_WAIT_LIMIT_SECS) {
					printk(UM_KERN_WARNING "%s : stub thread %d slow to respond (host state %c), still waiting\n",
					       __func__, st->tid, state);
					start = now;
					continue;
				}
				printk(UM_KERN_ERR "%s : stub thread %d did not respond (host state %c)\n",
				       __func__, st->tid, state ? state : '?');
				return -1;
			}
		}
	}
}

/*
 * Wake @st (unless @running) and wait for it to report back from its signal
 * handler, for at most STUB_WAIT_MAX_SECS if @bounded. Returns 0, or -1 if
 * the stub process died or misbehaved.
 */
int wait_stub_done_seccomp(struct stub_thread *st, int running,
			   int wait_sigsys, int bounded)
{
	struct mm_id *mm_idp = st->mm;
	struct stub_data *data = (void *)st->stack;

	do {
		if (!running)
			stub_wake(st);

		if (stub_wait(st, 1, bounded))
			goto out_kill;

		if (__READ_ONCE(mm_idp->pid) < 0)
			goto out_kill;

		running = 0;

		/* We may receive a SIGALRM before SIGSYS, iterate again. */
	} while (wait_sigsys && data->signal == SIGALRM);

	/*
	 * Both halves matter. The subtraction underflows when a signal frame is
	 * larger than sigstack[], which is not hypothetical: an arm64 mcontext_t
	 * is around 4.4 KB against x86-64's few hundred bytes, so with 4 KB
	 * pages it is bigger than the single page sigstack[] is. Without the
	 * first test the bound becomes SIZE_MAX and every offset passes it,
	 * which turns a stub that cannot work on this host into an
	 * out-of-bounds read that looks like a crash somewhere else.
	 */
	if (sizeof(data->sigstack) < sizeof(mcontext_t) ||
	    data->mctx_offset > sizeof(data->sigstack) - sizeof(mcontext_t)) {
		printk(UM_KERN_ERR "%s : invalid mcontext offset", __func__);
		goto out_kill;
	}

	if (wait_sigsys && data->signal != SIGSYS) {
		printk(UM_KERN_ERR "%s : expected SIGSYS but got %d",
		       __func__, data->signal);
		goto out_kill;
	}

	return 0;

out_kill:
	printk(UM_KERN_ERR "%s : failed to wait for stub, pid = %d, tid = %d, errno = %d\n",
	       __func__, mm_idp->pid, st->tid, errno);
	return -1;
}

extern unsigned long current_stub_stack(void);

static void get_skas_faultinfo(int pid, struct faultinfo *fi)
{
	int err;

	err = ptrace(PTRACE_CONT, pid, 0, SIGSEGV);
	if (err) {
		printk(UM_KERN_ERR "Failed to continue stub, pid = %d, "
		       "errno = %d\n", pid, errno);
		fatal_sigsegv();
	}
	wait_stub_done(pid);

	/*
	 * faultinfo is prepared by the stub_segv_handler at start of
	 * the stub stack page. We just have to copy it.
	 */
	memcpy(fi, (void *)current_stub_stack(), sizeof(*fi));
}

static void handle_trap(struct uml_pt_regs *regs)
{
	if ((UPT_IP(regs) >= STUB_START) && (UPT_IP(regs) < STUB_END))
		fatal_sigsegv();

	handle_syscall(regs);
}

extern char __syscall_stub_start[];

static int stub_exe_fd;

struct tramp_data {
	struct stub_data *stub_data;
	/* 0 is inherited, 1 is the kernel side */
	int sockpair[2];
};

#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC	(1U << 2)
#endif

static int userspace_tramp(void *data)
{
	struct tramp_data *tramp_data = data;
	char *const argv[] = { "uml-userspace", NULL };
	unsigned long long offset;
	struct stub_init_data init_data = {
		.seccomp = using_seccomp,
		.arch_flags = stub_arch_init_flags,
		.stub_start = STUB_START,
	};
	int ret;

	if (using_seccomp) {
		init_data.signal_handler = STUB_CODE +
					   (unsigned long) stub_signal_interrupt -
					   (unsigned long) __syscall_stub_start;
		init_data.signal_restorer = STUB_CODE +
					   (unsigned long) stub_signal_restorer -
					   (unsigned long) __syscall_stub_start;
	} else {
		init_data.signal_handler = STUB_CODE +
					   (unsigned long) stub_segv_handler -
					   (unsigned long) __syscall_stub_start;
		init_data.signal_restorer = 0;
	}

	init_data.stub_code_fd = phys_mapping(uml_to_phys(__syscall_stub_start),
					      &offset);
	init_data.stub_code_offset = MMAP_OFFSET(offset);

	init_data.stub_data_fd = phys_mapping(uml_to_phys(tramp_data->stub_data),
					      &offset);
	init_data.stub_data_offset = MMAP_OFFSET(offset);

	/*
	 * Avoid leaking unneeded FDs to the stub by setting CLOEXEC on all FDs
	 * and then unsetting it on all memory related FDs.
	 * This is not strictly necessary from a safety perspective.
	 */
	syscall(__NR_close_range, 0, ~0U, CLOSE_RANGE_CLOEXEC);

	fcntl(init_data.stub_data_fd, F_SETFD, 0);

	/* dup2 signaling FD/socket to STDIN */
	if (dup2(tramp_data->sockpair[0], 0) < 0)
		exit(3);
	close(tramp_data->sockpair[0]);

	/* Write init_data and close write side */
	ret = write(tramp_data->sockpair[1], &init_data, sizeof(init_data));
	close(tramp_data->sockpair[1]);

	if (ret != sizeof(init_data))
		exit(4);

	/* Raw execveat for compatibility with older libc versions */
	syscall(__NR_execveat, stub_exe_fd, (unsigned long)"",
		(unsigned long)argv, NULL, AT_EMPTY_PATH);

	exit(5);
}

extern char stub_exe_start[];
extern char stub_exe_end[];

extern char *tempdir;

#define STUB_EXE_NAME_TEMPLATE "/uml-userspace-XXXXXX"

#ifndef MFD_EXEC
#define MFD_EXEC 0x0010U
#endif

static int __init init_stub_exe_fd(void)
{
	size_t written = 0;
	char *tmpfile = NULL;

	stub_exe_fd = memfd_create("uml-userspace",
				   MFD_EXEC | MFD_CLOEXEC | MFD_ALLOW_SEALING);

	if (stub_exe_fd < 0) {
		printk(UM_KERN_INFO "Could not create executable memfd, using temporary file!");

		tmpfile = malloc(strlen(tempdir) +
				  strlen(STUB_EXE_NAME_TEMPLATE) + 1);
		if (tmpfile == NULL)
			panic("Failed to allocate memory for stub binary name");

		strcpy(tmpfile, tempdir);
		strcat(tmpfile, STUB_EXE_NAME_TEMPLATE);

		stub_exe_fd = mkstemp(tmpfile);
		if (stub_exe_fd < 0)
			panic("Could not create temporary file for stub binary: %d",
			      -errno);
	}

	while (written < stub_exe_end - stub_exe_start) {
		ssize_t res = write(stub_exe_fd, stub_exe_start + written,
				    stub_exe_end - stub_exe_start - written);
		if (res < 0) {
			if (errno == EINTR)
				continue;

			if (tmpfile)
				unlink(tmpfile);
			panic("Failed write stub binary: %d", -errno);
		}

		written += res;
	}

	if (!tmpfile) {
		fcntl(stub_exe_fd, F_ADD_SEALS,
		      F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL);
	} else {
		if (fchmod(stub_exe_fd, 00500) < 0) {
			unlink(tmpfile);
			panic("Could not make stub binary executable: %d",
			      -errno);
		}

		close(stub_exe_fd);
		stub_exe_fd = open(tmpfile, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
		if (stub_exe_fd < 0) {
			unlink(tmpfile);
			panic("Could not reopen stub binary: %d", -errno);
		}

		unlink(tmpfile);
		free(tmpfile);
	}

	return 0;
}
__initcall(init_stub_exe_fd);

int using_seccomp;

/* Backend-interpreted bits for stub_arch_init(); see skas.h. */
unsigned long stub_arch_init_flags;

/*
 * Assume the host has PTRACE_SYSEMU until check_sysemu() says
 * otherwise; see skas.h. arm64 hosts older than 5.3 flip it to 0.
 */
int have_ptrace_sysemu = 1;
int syscall_cancel_nr = -1;

/**
 * start_userspace() - prepare a new userspace process
 * @mm_id: The corresponding struct mm_id
 *
 * Setups a new temporary stack page that is used while userspace_tramp() runs
 * Clones the kernel process into a new userspace process, with FDs only.
 *
 * Return: When positive: the process id of the new userspace process,
 *         when negative: an error number.
 * FIXME: can PIDs become negative?!
 */
int start_userspace(struct mm_id *mm_id)
{
	struct stub_data *proc_data = (void *)mm_id->main.stack;
	struct tramp_data tramp_data = {
		.stub_data = proc_data,
	};
	void *stack;
	unsigned long sp;
	int status, n, err;

	/* setup a temporary stack page */
	stack = mmap(NULL, UM_KERN_PAGE_SIZE,
		     PROT_READ | PROT_WRITE | PROT_EXEC,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (stack == MAP_FAILED) {
		err = -errno;
		printk(UM_KERN_ERR "%s : mmap failed, errno = %d\n",
		       __func__, errno);
		return err;
	}

	/* set stack pointer to the end of the stack page, so it can grow downwards */
	sp = (unsigned long)stack + UM_KERN_PAGE_SIZE;

	/* socket pair for init data and SECCOMP FD passing (no CLOEXEC here) */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, tramp_data.sockpair)) {
		err = -errno;
		printk(UM_KERN_ERR "%s : socketpair failed, errno = %d\n",
		       __func__, errno);
		return err;
	}

	if (using_seccomp)
		proc_data->futex = FUTEX_IN_CHILD;

	/* The stub process serves all CPUs: not only this one's host CPU */
	os_affinity_widen();
	mm_id->pid = clone(userspace_tramp, (void *) sp,
		    CLONE_VFORK | CLONE_VM | SIGCHLD,
		    (void *)&tramp_data);
	os_affinity_restore();
	if (mm_id->pid < 0) {
		err = -errno;
		printk(UM_KERN_ERR "%s : clone failed, errno = %d\n",
		       __func__, errno);
		goto out_close;
	}

	mm_id->main.mm = mm_id;
	mm_id->main.slot = 0;
	mm_id->main.tid = mm_id->pid;

	if (using_seccomp) {
		if (wait_stub_done_seccomp(&mm_id->main, 1, 1, 1)) {
			err = -EIO;
			goto out_kill;
		}
	} else {
		do {
			CATCH_EINTR(n = waitpid(mm_id->pid, &status,
						WUNTRACED | __WALL));
			if (n < 0) {
				err = -errno;
				printk(UM_KERN_ERR "%s : wait failed, errno = %d\n",
				       __func__, errno);
				goto out_kill;
			}
		} while (WIFSTOPPED(status) && (WSTOPSIG(status) == SIGALRM));

		if (!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGSTOP)) {
			err = -EINVAL;
			printk(UM_KERN_ERR "%s : expected SIGSTOP, got status = %d\n",
			       __func__, status);
			goto out_kill;
		}

		if (ptrace(PTRACE_SETOPTIONS, mm_id->pid, NULL,
			   (void *) PTRACE_O_TRACESYSGOOD) < 0) {
			err = -errno;
			printk(UM_KERN_ERR "%s : PTRACE_SETOPTIONS failed, errno = %d\n",
			       __func__, errno);
			goto out_kill;
		}
	}

	if (munmap(stack, UM_KERN_PAGE_SIZE) < 0) {
		err = -errno;
		printk(UM_KERN_ERR "%s : munmap failed, errno = %d\n",
		       __func__, errno);
		goto out_kill;
	}

	close(tramp_data.sockpair[0]);
	if (using_seccomp)
		mm_id->sock = tramp_data.sockpair[1];
	else
		close(tramp_data.sockpair[1]);

	return 0;

out_kill:
	os_kill_ptraced_process(mm_id->pid, 1);
out_close:
	close(tramp_data.sockpair[0]);
	close(tramp_data.sockpair[1]);

	mm_id->pid = -1;

	return err;
}

#ifdef CONFIG_UML_MT_STUB
/**
 * start_stub_thread() - create another host thread in a stub process
 * @st: The new thread, with st->mm, st->slot and st->stack set up
 *
 * The thread that exec()ed the stub maps the data area of @st into its slot
 * and clones the new thread, which then waits in its signal handler until it
 * is told to run userspace.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int start_stub_thread(struct stub_thread *st)
{
	struct stub_thread *main = &st->mm->main;
	struct stub_data *data = (void *)st->stack;
	unsigned long long offset;
	int fd, err;

	data->futex = FUTEX_IN_CHILD;
	fd = phys_mapping(uml_to_phys(data), &offset);

	map(main, STUB_SLOT_DATA(st->slot), STUB_SLOT_SIZE,
	    UM_PROT_READ | UM_PROT_WRITE, fd, offset);
	stub_thread_clone(main, st->slot);

	err = syscall_stub_flush(main);
	if (err)
		return err;

	if (wait_stub_done_seccomp(st, 1, 0, 1))
		return -EIO;

	/* Negative if the thread could not set up its signal stack */
	if (data->tid <= 0)
		return -EIO;
	st->tid = data->tid;

	/* It runs userspace for one CPU: on that CPU's host CPU */
	os_pin_stub_thread(st->mm->pid, st->tid, st->slot - 1);

	return 0;
}
#endif

/* Interrupt a stub thread that may be running userspace */
void stub_thread_kick(struct stub_thread *st)
{
	if (st->mm->pid <= 0 || st->tid <= 0)
		return;

	syscall(__NR_tgkill, st->mm->pid, st->tid, SIGALRM);
}

/*
 * Interrupt a stub thread that may be running userspace, and wait until it is
 * back in its signal handler. Called from an IPI handler on the CPU that runs
 * the thread, which may have interrupted any step of running it.
 */
void stub_thread_stop(struct stub_thread *st)
{
	struct stub_data *data = (void *)st->stack;

	/*
	 * Being killed (possibly by this very CPU, interrupted in stub_kill())
	 * or dead: its threads run no userspace that would need stopping.
	 */
	if (__READ_ONCE(st->mm->pid) <= 0)
		return;

	stub_thread_kick(st);

	/*
	 * stub_wake() may have been interrupted between setting
	 * FUTEX_IN_CHILD and waking the thread up; nothing would wake it
	 * until this returns.
	 */
	if (__atomic_load_n(&data->futex, __ATOMIC_ACQUIRE) == FUTEX_IN_CHILD)
		syscall(__NR_futex, &data->futex, FUTEX_WAKE, 1, NULL, NULL, 0);

	/*
	 * A thread that is not running stays in its handler (FUTEX_IN_KERN);
	 * one that runs traps on the kick. If the stub process died or does
	 * not respond, the CPU's own wait for it deals with that.
	 */
	stub_wait(st, 1, 1);
}

static int unscheduled_userspace_iterations;
extern unsigned long tt_extra_sched_jiffies;

#ifdef UM_SYSCALL_TRAP_INSN
/*
 * Is the guest about to execute the instruction that enters the
 * kernel?
 *
 * Only consulted while single-stepping a guest on a host without
 * PTRACE_SYSEMU, which is a debugger path, so the extra ptrace call
 * per step costs nothing that matters. A failed read is reported as
 * "not a syscall": the instruction is about to be executed by the
 * guest either way, and if it is unreadable the single-step will fault
 * on it and be reported normally, which is a better outcome than
 * refusing to step.
 */
static int at_syscall_insn(int pid, unsigned long pc)
{
	long word;

	errno = 0;
	word = ptrace(PTRACE_PEEKTEXT, pid, (void *)pc, 0);
	if (word == -1 && errno)
		return 0;

	return (unsigned int)word == UM_SYSCALL_TRAP_INSN;
}
#endif

void userspace(struct uml_pt_regs *regs)
{
	int err, status, op;
	siginfo_t si_local;
	siginfo_t *si;
	int sig;

	/* Handle any immediate reschedules or signals */
	interrupt_end();

	while (1) {
		struct mm_id *mm_id = current_mm_id();
		struct stub_thread *st;

		/*
		 * Pick the stub thread to run on, and serialise against other
		 * CPUs using the same stub process. On success, preemption is
		 * disabled until leave_userspace().
		 */
		st = enter_userspace(mm_id);
		if (!st)
			fatal_sigsegv();

		/*
		 * When we are in time-travel mode, userspace can theoretically
		 * do a *lot* of work without being scheduled. The problem with
		 * this is that it will prevent kernel bookkeeping (primarily
		 * the RCU) from running and this can for example cause OOM
		 * situations.
		 *
		 * This code accounts a jiffie against the scheduling clock
		 * after the defined userspace iterations in the same thread.
		 * By doing so the situation is effectively prevented.
		 */
		if (time_travel_mode == TT_MODE_INFCPU ||
		    time_travel_mode == TT_MODE_EXTERNAL) {
#ifdef CONFIG_UML_MAX_USERSPACE_ITERATIONS
			if (CONFIG_UML_MAX_USERSPACE_ITERATIONS &&
			    unscheduled_userspace_iterations++ >
			    CONFIG_UML_MAX_USERSPACE_ITERATIONS) {
				tt_extra_sched_jiffies += 1;
				unscheduled_userspace_iterations = 0;
			}
#endif
		}

		time_travel_print_bc_msg();

		if (using_seccomp) {
			struct stub_data *proc_data = (void *)st->stack;
			unsigned short si_offset;
			int nr_syscalls;
			int ack;

			err = set_stub_state(regs, proc_data, singlestepping());
			if (err) {
				printk(UM_KERN_ERR "%s - failed to set regs: %d",
				       __func__, err);
				goto fatal;
			}

			/* Must have been reset by the syscall caller */
			if (proc_data->restart_wait != 0) {
				printk(UM_KERN_ERR "%s - flag to only run syscalls in stub was not cleared",
				       __func__);
				goto fatal;
			}

			/* Mark pending syscalls for flushing */
			nr_syscalls = st->syscall_data_len;
			proc_data->syscall_data_len = nr_syscalls;

			/*
			 * If sync_tlb_lock is held (see enter_userspace()),
			 * keep it until the queued syscalls are done.
			 */
			ack = st->sync_locked && st->syscall_data_len;
			proc_data->ack_syscalls = ack;
			if (!ack)
				userspace_running(mm_id, st);

			stub_wake(st);

			if (ack) {
				if (stub_wait(st, 0, 1))
					goto fatal_wait;
				/*
				 * If a request failed, the stub reports back
				 * without FUTEX_SYSCALLS_DONE. Kill it before
				 * anybody else relies on the host mappings.
				 */
				if (__atomic_load_n(&proc_data->futex, __ATOMIC_ACQUIRE) !=
				    FUTEX_SYSCALLS_DONE &&
				    proc_data->err) {
					printk(UM_KERN_ERR "%s - Error flushing stub syscalls",
					       __func__);
					syscall_stub_dump_error(st);
					goto fatal;
				}
				userspace_running(mm_id, st);
			}

			if (wait_stub_done_seccomp(st, 1, 0, 0))
				goto fatal;

			sig = proc_data->signal;

			if (nr_syscalls && proc_data->err != 0) {
				printk(UM_KERN_ERR "%s - Error flushing stub syscalls",
				       __func__);
				syscall_stub_dump_error(st);
				goto fatal;
			}

			st->syscall_data_len = 0;
			st->syscall_fd_num = 0;

			err = get_stub_state(regs, proc_data, NULL);
			if (err) {
				printk(UM_KERN_ERR "%s - failed to get regs: %d",
				       __func__, err);
				goto fatal;
			}

			si_offset = __READ_ONCE(proc_data->si_offset);
			if (si_offset > sizeof(proc_data->sigstack) - sizeof(*si)) {
				printk(UM_KERN_ERR "%s - Invalid siginfo offset from stub",
				       __func__);
				goto fatal;
			}

			si = &si_local;
			memcpy(si, &proc_data->sigstack[si_offset], sizeof(*si));

			regs->is_user = 1;

			/* Fill in ORIG_RAX and extract fault information */
			PT_SYSCALL_NR(regs->gp) = si->si_syscall;
			if (sig == SIGSEGV) {
				unsigned short mctx_offset = __READ_ONCE(proc_data->mctx_offset);
				mcontext_t *mcontext;

				if (mctx_offset > sizeof(proc_data->sigstack) - sizeof(*mcontext)) {
					printk(UM_KERN_ERR "%s - Invalid mcontext offset from stub",
					       __func__);
					goto fatal;
				}
				mcontext = (void *)&proc_data->sigstack[mctx_offset];

				GET_FAULTINFO_FROM_MC(regs->faultinfo, mcontext);
			}
		} else {
			int pid = mm_id->pid;

			/* Flush out any pending syscalls */
			err = syscall_stub_flush(st);
			if (err) {
				if (err == -ENOMEM)
					report_enomem();

				printk(UM_KERN_ERR "%s - Error flushing stub syscalls: %d",
					__func__, -err);
				goto fatal;
			}

			/*
			 * This can legitimately fail if the process loads a
			 * bogus value into a segment register.  It will
			 * segfault and ptrace_getregs will read that value
			 * out of the process.  However, ptrace_setregs will
			 * fail.  In this case, there is nothing to do but
			 * just kill the process.
			 */
			if (ptrace_setregs(pid, regs->gp)) {
				printk(UM_KERN_ERR "%s - ptrace set regs failed, errno = %d\n",
				       __func__, errno);
				goto fatal;
			}

			if (put_fp_registers(pid, regs->fp)) {
				printk(UM_KERN_ERR "%s - ptrace set fp regs failed, errno = %d\n",
				       __func__, errno);
				goto fatal;
			}

			/*
			 * Without PTRACE_SYSEMU the guest runs under plain
			 * PTRACE_SYSCALL and the syscall is cancelled or
			 * substituted at the entry stop below, which reaches
			 * the same place: the guest's call does not execute
			 * and the guest resumes past it with whatever UML
			 * puts in the return register.
			 *
			 * On an architecture without UM_SYSCALL_TRAP_INSN,
			 * forcing this path (nosysemu) degrades guest
			 * single-stepping to PTRACE_SYSCALL: with no way to
			 * recognize the kernel-entry instruction, stepping
			 * stays at syscall granularity. Guest single-step is
			 * only the debugger-switch path, and coarse stepping
			 * there is preferred over ever letting a guest
			 * syscall run on the host.
			 */
			if (!have_ptrace_sysemu) {
				op = PTRACE_SYSCALL;
#ifdef UM_SYSCALL_TRAP_INSN
				/*
				 * Single-stepping still has to step. The one
				 * instruction that must not be stepped is the
				 * one that enters the kernel: stepping it
				 * would run the guest's syscall on the host.
				 * Take the syscall stop for that instruction
				 * instead, so it can be cancelled like any
				 * other.
				 */
				if (singlestepping() &&
				    !at_syscall_insn(pid, regs->gp[REGS_IP_INDEX]))
					op = PTRACE_SINGLESTEP;
#endif
			} else if (singlestepping()) {
				op = PTRACE_SYSEMU_SINGLESTEP;
			} else {
				op = PTRACE_SYSEMU;
			}

			if (ptrace(op, pid, 0, 0)) {
				printk(UM_KERN_ERR "%s - ptrace continue failed, op = %d, errno = %d\n",
				       __func__, op, errno);
				goto fatal;
			}

			CATCH_EINTR(err = waitpid(pid, &status, WUNTRACED | __WALL));
			if (err < 0) {
				printk(UM_KERN_ERR "%s - wait failed, errno = %d\n",
				       __func__, errno);
				goto fatal;
			}

			regs->is_user = 1;
			if (ptrace_getregs(pid, regs->gp)) {
				printk(UM_KERN_ERR "%s - ptrace_getregs failed, errno = %d\n",
				       __func__, errno);
				goto fatal;
			}

			if (get_fp_registers(pid, regs->fp)) {
				printk(UM_KERN_ERR "%s -  get_fp_registers failed, errno = %d\n",
				       __func__, errno);
				goto fatal;
			}

			/*
			 * Two independent reasons to single-step off a
			 * syscall-entry stop before resuming the loop:
			 *
			 * On an architecture with UM_SYSCALL_STOP_HIDES_REG,
			 * one register is not visible at a syscall stop and
			 * cannot be written there either; see
			 * <sysdep/ptrace_user.h>. Everything else has just
			 * been read correctly, so step off the stop and pick
			 * up that one register alone.
			 *
			 * The step runs no guest code: the syscall stays
			 * emulated away and the program counter does not move.
			 * It is still not a free stop to stand on, because it
			 * arrives as a forced SIGTRAP, so the host's signal
			 * path runs and clears the recorded syscall number,
			 * and would rewind the program counter if the first
			 * syscall argument happened to look like -ERESTARTSYS.
			 * Taking only the hidden register from here, and
			 * everything else from the syscall stop above, is
			 * immune to both: whatever the host did to the rest is
			 * overwritten by the register write that resumes this
			 * task.
			 *
			 * The other half of the reason to step is the write:
			 * the task is left parked on a stop where the full
			 * register set can be installed, which is what a guest
			 * thread switch needs and what a syscall stop silently
			 * refuses.
			 *
			 * Without PTRACE_SYSEMU the step is needed on every
			 * architecture: this stop is a real syscall entry, so
			 * the call must be cancelled or substituted first and
			 * the (defused) syscall then stepped over, which also
			 * consumes what would otherwise surface as a
			 * syscall-exit stop on the next resume.
			 */
			if (WIFSTOPPED(status) &&
			    WSTOPSIG(status) == (SIGTRAP | 0x80) &&
			    (!have_ptrace_sysemu || UM_SYSCALL_STOP_HIDDEN)) {
				int sstatus, tries = 0;

				/*
				 * syscall_cancel_nr is -1, the documented
				 * "run nothing" value, unless the boot probe
				 * found the host refuses it, in which case it
				 * names a harmless syscall to run in the
				 * guest call's place. See check_sysemu(). The
				 * registers were read above, so the syscall
				 * number UML needs has already been taken and
				 * overwriting it here loses nothing.
				 */
				if (!have_ptrace_sysemu &&
				    sysdep_ptrace_pokeuser(pid,
							   PT_SYSCALL_NR_OFFSET,
							   syscall_cancel_nr)) {
					printk(UM_KERN_ERR "%s - failed to cancel a guest syscall, errno = %d\n",
					       __func__, errno);
					fatal_sigsegv();
				}

				while (1) {
					if (ptrace(PTRACE_SINGLESTEP, pid, 0, 0)) {
						printk(UM_KERN_ERR "%s - failed to step off a syscall stop, errno = %d\n",
						       __func__, errno);
						fatal_sigsegv();
					}

					CATCH_EINTR(err = waitpid(pid, &sstatus,
								  WUNTRACED | __WALL));
					if (err < 0) {
						printk(UM_KERN_ERR "%s - wait after step failed, errno = %d\n",
						       __func__, errno);
						fatal_sigsegv();
					}

					if (WIFSTOPPED(sstatus) &&
					    WSTOPSIG(sstatus) == SIGTRAP)
						break;

					/*
					 * The stub can be interrupted here, and
					 * wait_stub_done() tolerates the same
					 * two signals for the same reason.
					 * Neither is for the guest: the stub
					 * installs no handler for either in
					 * ptrace mode, so delivering one would
					 * kill it. The step has not happened
					 * yet, so drop the signal and step
					 * again, bounded so a stub that will
					 * never trap still fails rather than
					 * spins.
					 */
					if (WIFSTOPPED(sstatus) &&
					    ((1 << WSTOPSIG(sstatus)) & STUB_SIG_MASK) &&
					    ++tries < 16)
						continue;

					/*
					 * Anything else means the assumption
					 * above no longer holds. Fail loudly
					 * rather than run a guest on registers
					 * that are quietly wrong.
					 */
					printk(UM_KERN_ERR "%s - unexpected status 0x%x stepping off a syscall stop\n",
					       __func__, sstatus);
					fatal_sigsegv();
				}

#ifdef UM_SYSCALL_STOP_HIDES_REG
				{
					unsigned long hidden[UM_GP_SLOTS];

					if (ptrace_getregs(pid, hidden)) {
						printk(UM_KERN_ERR "%s - ptrace_getregs after step failed, errno = %d\n",
						       __func__, errno);
						fatal_sigsegv();
					}

					regs->gp[UM_SYSCALL_STOP_HIDDEN_REG] =
						hidden[UM_SYSCALL_STOP_HIDDEN_REG];
				}
#endif
			}

			if (WIFSTOPPED(status)) {
				sig = WSTOPSIG(status);

				/*
				 * These signal handlers need the si argument
				 * and SIGSEGV needs the faultinfo.
				 * The SIGIO and SIGALARM handlers which constitute
				 * the majority of invocations, do not use it.
				 */
				switch (sig) {
				case SIGSEGV:
					get_skas_faultinfo(pid,
							   &regs->faultinfo);
					fallthrough;
				case SIGTRAP:
				case SIGILL:
				case SIGBUS:
				case SIGFPE:
				case SIGWINCH:
					ptrace(PTRACE_GETSIGINFO, pid, 0,
					       (struct siginfo *)&si_local);
					si = &si_local;
					break;
				default:
					si = NULL;
					break;
				}
			} else {
				sig = 0;
			}
		}

		leave_userspace(mm_id, st);

		UPT_SYSCALL_NR(regs) = -1; /* Assume: It's not a syscall */

		if (sig) {
			switch (sig) {
			case SIGSEGV:
				if (using_seccomp || PTRACE_FULL_FAULTINFO)
					(*sig_info[SIGSEGV])(SIGSEGV,
							     (struct siginfo *)si,
							     regs, NULL);
				else
					segv(regs->faultinfo, 0, 1, NULL, NULL);

				break;
			case SIGSYS:
				handle_syscall(regs);
				break;
			case SIGTRAP + 0x80:
				handle_trap(regs);
				break;
			case SIGTRAP:
				relay_signal(SIGTRAP, (struct siginfo *)si, regs, NULL);
				break;
			case SIGALRM:
				break;
			case SIGIO:
			case SIGBUS:
			case SIGFPE:
			case SIGWINCH:
				block_signals_trace();
				(*sig_info[sig])(sig, (struct siginfo *)si, regs, NULL);
				unblock_signals_trace();
				break;
			case SIGILL:
				if (arch_sigill_fixup(regs))
					break;
				block_signals_trace();
				(*sig_info[sig])(sig, (struct siginfo *)si, regs, NULL);
				unblock_signals_trace();
				break;
			default:
				printk(UM_KERN_ERR "%s - child stopped with signal %d\n",
				       __func__, sig);
				fatal_sigsegv();
			}
			interrupt_end();

			/* Avoid -ERESTARTSYS handling in host */
			if (PT_SYSCALL_NR_OFFSET != PT_SYSCALL_RET_OFFSET)
				PT_SYSCALL_NR(regs->gp) = -1;
		}
		continue;

fatal_wait:
		printk(UM_KERN_ERR "%s : failed to wait for stub, pid = %d, tid = %d\n",
		       __func__, mm_id->pid, st->tid);
fatal:
		userspace_failed(mm_id, st);
		leave_userspace(mm_id, st);
		fatal_sigsegv();
	}
}

void new_thread(void *stack, jmp_buf *buf, void (*handler)(void))
{
	(*buf)[0].JB_IP = (unsigned long) handler;
	(*buf)[0].JB_SP = UM_THREAD_START_SP(stack);
}

#define INIT_JMP_NEW_THREAD 0
#define INIT_JMP_CALLBACK 1
#define INIT_JMP_HALT 2
#define INIT_JMP_REBOOT 3

void switch_threads(jmp_buf *me, jmp_buf *you)
{
	unscheduled_userspace_iterations = 0;

	if (UML_SETJMP(me) == 0)
		UML_LONGJMP(you, 1);
}

static jmp_buf initial_jmpbuf;

static __thread void (*cb_proc)(void *arg);
static __thread void *cb_arg;
static __thread jmp_buf *cb_back;

void start_idle_thread(void *stack, jmp_buf *switch_buf)
{
	int n;

	set_handler(SIGWINCH);

	/*
	 * Can't use UML_SETJMP or UML_LONGJMP here because they save
	 * and restore signals, with the possible side-effect of
	 * trying to handle any signals which came when they were
	 * blocked, which can't be done on this stack.
	 * Signals must be blocked when jumping back here and restored
	 * after returning to the jumper.
	 */
	n = setjmp(initial_jmpbuf);
	switch (n) {
	case INIT_JMP_NEW_THREAD:
		(*switch_buf)[0].JB_IP = (unsigned long) uml_finishsetup;
		(*switch_buf)[0].JB_SP = UM_THREAD_START_SP(stack);
		break;
	case INIT_JMP_CALLBACK:
		(*cb_proc)(cb_arg);
		longjmp(*cb_back, 1);
		break;
	case INIT_JMP_HALT:
		kmalloc_ok = 0;
		os_exit(false);
	case INIT_JMP_REBOOT:
		kmalloc_ok = 0;
		os_exit(true);
	default:
		printk(UM_KERN_ERR "Bad sigsetjmp return in %s - %d\n",
		       __func__, n);
		fatal_sigsegv();
	}
	longjmp(*switch_buf, 1);
}

void initial_thread_cb_skas(void (*proc)(void *), void *arg)
{
	jmp_buf here;

	cb_proc = proc;
	cb_arg = arg;
	cb_back = &here;

	initial_jmpbuf_lock();
	if (UML_SETJMP(&here) == 0)
		UML_LONGJMP(&initial_jmpbuf, INIT_JMP_CALLBACK);
	initial_jmpbuf_unlock();

	cb_proc = NULL;
	cb_arg = NULL;
	cb_back = NULL;
}

void halt_skas(void)
{
	initial_jmpbuf_lock();
	UML_LONGJMP(&initial_jmpbuf, INIT_JMP_HALT);
	/* unreachable */
}

static bool noreboot;

static int __init noreboot_cmd_param(char *str, int *add)
{
	*add = 0;
	noreboot = true;
	return 0;
}

__uml_setup("noreboot", noreboot_cmd_param,
"noreboot\n"
"    Rather than rebooting, exit always, akin to QEMU's -no-reboot option.\n"
"    This is useful if you're using CONFIG_PANIC_TIMEOUT in order to catch\n"
"    crashes in CI\n\n");

void reboot_skas(void)
{
	initial_jmpbuf_lock();
	UML_LONGJMP(&initial_jmpbuf, noreboot ? INIT_JMP_HALT : INIT_JMP_REBOOT);
	/* unreachable */
}
