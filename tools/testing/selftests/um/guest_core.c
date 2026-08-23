// SPDX-License-Identifier: GPL-2.0
/*
 * In-guest core battery for UML: process management, memory faults,
 * signal frames, TLS, and syscall restart.
 *
 * Every subtest here earned its place by failing on a real UML port at
 * some point.  The ones worth calling out:
 *
 *   restart_read / restart_nanosleep
 *	A syscall interrupted by a signal is re-executed by rewinding the
 *	PC to the trap instruction, which on several architectures re-reads
 *	BOTH the syscall-number register and the first argument register
 *	(arm64: x8 and x0).  A port that restores only the return-value
 *	register on restart turns a restarted read() into some other
 *	syscall entirely.  These two cover the ERESTARTSYS path (handler
 *	with SA_RESTART) and the ERESTART_RESTARTBLOCK path (nanosleep
 *	resumed after a discarded signal).
 *
 *   tls_fork / tls_thread_fork
 *	The child of fork() must inherit the TLS pointer of the THREAD
 *	that called fork, and keep it across subsequent stops.  A port
 *	that tracks TLS per host stub process rather than per guest
 *	thread loses this as soon as the child is switched in.
 *
 *   sig_fpstate / sig_regs / sig_mask_restore / sigaltstack
 *	Signal delivery and sigreturn round-trip the full register state
 *	through the guest signal frame.  FP state, callee- and
 *	caller-saved integer registers, and the signal mask must all
 *	survive; the mask in particular is restored by sigreturn from the
 *	frame the guest kernel wrote.
 *
 * Each subtest runs in its own forked child with a self-imposed alarm,
 * so a hang in one is reported as a failure rather than hanging the
 * whole battery.  Exit codes: 0 pass, 4 skip, anything else fail.
 *
 * Copyright (C) 2026 Fabian Franz
 */
#include "um_guest.h"

#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/time.h>

#define SUB_PASS 0
#define SUB_FAIL 1
#define SUB_SKIP 4

#define SUBTEST_TIMEOUT 30

static char *self_exe;

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- process management ------------------------------------------------ */

static int sub_fork_wait(void)
{
	int i, st;
	pid_t pids[8];

	for (i = 0; i < 8; i++) {
		pids[i] = fork();
		if (pids[i] < 0)
			return SUB_FAIL;
		if (pids[i] == 0)
			_exit(50 + i);
	}
	for (i = 0; i < 8; i++) {
		if (waitpid(pids[i], &st, 0) != pids[i])
			return SUB_FAIL;
		if (!WIFEXITED(st) || WEXITSTATUS(st) != 50 + i)
			return SUB_FAIL;
	}
	return SUB_PASS;
}

static int sub_fork_pipe(void)
{
	char buf[4096], expect[4096];
	int fds[2], i;
	ssize_t got, n;
	pid_t pid;
	int st;

	for (i = 0; i < (int)sizeof(expect); i++)
		expect[i] = (char)(i * 7 + 3);

	if (pipe(fds))
		return SUB_FAIL;
	pid = fork();
	if (pid < 0)
		return SUB_FAIL;
	if (pid == 0) {
		close(fds[0]);
		for (i = 0; i < (int)sizeof(expect); ) {
			n = write(fds[1], expect + i, sizeof(expect) - i);
			if (n <= 0)
				_exit(1);
			i += n;
		}
		_exit(0);
	}
	close(fds[1]);
	for (got = 0; got < (ssize_t)sizeof(buf); ) {
		n = read(fds[0], buf + got, sizeof(buf) - got);
		if (n < 0)
			return SUB_FAIL;
		if (n == 0)
			break;
		got += n;
	}
	close(fds[0]);
	waitpid(pid, &st, 0);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
		return SUB_FAIL;
	if (got != (ssize_t)sizeof(buf) || memcmp(buf, expect, sizeof(buf)))
		return SUB_FAIL;
	return SUB_PASS;
}

static int sub_wait_signal(void)
{
	pid_t pid;
	int st;

	pid = fork();
	if (pid < 0)
		return SUB_FAIL;
	if (pid == 0) {
		raise(SIGKILL);
		_exit(1);
	}
	if (waitpid(pid, &st, 0) != pid)
		return SUB_FAIL;
	if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGKILL)
		return SUB_FAIL;
	return SUB_PASS;
}

static int sub_execve(void)
{
	pid_t pid;
	int st;

	pid = fork();
	if (pid < 0)
		return SUB_FAIL;
	if (pid == 0) {
		char *argvv[] = { self_exe, (char *)"--exec-child", NULL };
		char *envv[] = { (char *)"UM_EXEC_MARK=um-selftest", NULL };

		execve(self_exe, argvv, envv);
		_exit(127);
	}
	waitpid(pid, &st, 0);
	if (!WIFEXITED(st))
		return SUB_FAIL;
	if (WEXITSTATUS(st) == 127)
		return SUB_FAIL;	/* exec itself failed */
	return WEXITSTATUS(st) == 42 ? SUB_PASS : SUB_FAIL;
}

/* ---- memory / fault paths ---------------------------------------------- */

static int sub_pagefault_pattern(void)
{
	long pagesz = sysconf(_SC_PAGESIZE);
	long pages = 1024, i;
	unsigned char *p;

	p = mmap(NULL, pages * pagesz, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return SUB_FAIL;
	for (i = 0; i < pages; i++)
		p[i * pagesz] = (unsigned char)(i & 0xff);
	for (i = 0; i < pages; i++)
		if (p[i * pagesz] != (unsigned char)(i & 0xff))
			return SUB_FAIL;
	/* A fresh anonymous page must read back zero around the write. */
	for (i = 0; i < pages; i++)
		if (p[i * pagesz + 1] != 0)
			return SUB_FAIL;
	munmap(p, pages * pagesz);
	return SUB_PASS;
}

static int sub_cow(void)
{
	long pagesz = sysconf(_SC_PAGESIZE);
	unsigned char *p;
	pid_t pid;
	int st;

	p = mmap(NULL, 4 * pagesz, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return SUB_FAIL;
	memset(p, 0x11, 4 * pagesz);

	pid = fork();
	if (pid < 0)
		return SUB_FAIL;
	if (pid == 0) {
		/* Child writes; parent must never see it. */
		memset(p, 0x22, 4 * pagesz);
		_exit(p[3 * pagesz] == 0x22 ? 0 : 1);
	}
	waitpid(pid, &st, 0);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
		return SUB_FAIL;
	if (p[0] != 0x11 || p[3 * pagesz] != 0x11)
		return SUB_FAIL;
	munmap(p, 4 * pagesz);
	return SUB_PASS;
}

static int sub_madv_dontneed(void)
{
	long pagesz = sysconf(_SC_PAGESIZE);
	unsigned char *p;

	p = mmap(NULL, 4 * pagesz, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return SUB_FAIL;
	memset(p, 0x33, 4 * pagesz);
	if (madvise(p, 4 * pagesz, MADV_DONTNEED))
		return SUB_SKIP;
	/* Refault: anonymous private memory must come back zero-filled. */
	if (p[0] != 0 || p[2 * pagesz + 5] != 0)
		return SUB_FAIL;
	munmap(p, 4 * pagesz);
	return SUB_PASS;
}

/*
 * The pointer itself must be volatile (unsigned char *volatile), not
 * merely point at volatile bytes: the assignment before the faulting
 * store has no data dependency on it, and -O2 otherwise sinks the
 * assignment below the store, so the handler compares against NULL.
 */
static unsigned char *volatile segv_expected_addr;
static volatile int segv_hits;
static long segv_pagesz;

static void segv_handler(int sig, siginfo_t *info, void *uc)
{
	(void)sig;
	(void)uc;
	segv_hits++;
	if (info->si_addr != segv_expected_addr || segv_hits > 1)
		_exit(SUB_FAIL);
	/* Make the faulting store succeed on retry. */
	mprotect((void *)segv_expected_addr, segv_pagesz,
		 PROT_READ | PROT_WRITE);
}

static int sub_mprotect_segv(void)
{
	struct sigaction sa;
	unsigned char *p;

	segv_pagesz = sysconf(_SC_PAGESIZE);
	p = mmap(NULL, segv_pagesz, PROT_READ,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return SUB_FAIL;

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = segv_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGSEGV, &sa, NULL))
		return SUB_FAIL;

	segv_expected_addr = p;
	segv_hits = 0;
	*(volatile unsigned char *)p = 0x44;	/* faults exactly once */

	if (segv_hits != 1 || p[0] != 0x44)
		return SUB_FAIL;
	munmap(p, segv_pagesz);
	return SUB_PASS;
}

/* ---- TLS across fork ---------------------------------------------------- */

static __thread unsigned long tls_word;

static int tls_check_child(unsigned long want)
{
	pid_t pid;
	int st;

	pid = fork();
	if (pid < 0)
		return SUB_FAIL;
	if (pid == 0) {
		/* errno lives in TLS too: exercise it with a real failure. */
		errno = 0;
		if (close(4242) != -1 || errno != EBADF)
			_exit(2);
		_exit(tls_word == want ? 0 : 1);
	}
	waitpid(pid, &st, 0);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
		return SUB_FAIL;
	if (tls_word != want)	/* and the parent's copy is untouched */
		return SUB_FAIL;
	return SUB_PASS;
}

static int sub_tls_fork(void)
{
	tls_word = 0xaa55aa55aa55aa55UL;
	return tls_check_child(0xaa55aa55aa55aa55UL);
}

static void *tls_thread_body(void *arg)
{
	(void)arg;
	tls_word = 0x1234567812345678UL;
	/*
	 * fork() FROM the thread: the child inherits this thread's TLS
	 * pointer, not the main thread's.
	 */
	return (void *)(long)tls_check_child(0x1234567812345678UL);
}

static int sub_tls_thread_fork(void)
{
	pthread_t th;
	void *ret;

	tls_word = 0x0badc0de0badc0deUL;	/* main thread's value differs */
	if (pthread_create(&th, NULL, tls_thread_body, NULL))
		return SUB_SKIP;
	if (pthread_join(th, &ret))
		return SUB_FAIL;
	if ((long)ret != SUB_PASS)
		return SUB_FAIL;
	if (tls_word != 0x0badc0de0badc0deUL)
		return SUB_FAIL;
	return SUB_PASS;
}

/* ---- syscall restart with pending signals ------------------------------- */

static volatile int alarms_seen;

static void count_alarm(int sig)
{
	(void)sig;
	alarms_seen++;
}

static int sub_restart_read(void)
{
	static const char msg[8] = "restart!";
	struct itimerval it;
	struct sigaction sa;
	char buf[8];
	int fds[2];
	pid_t pid;
	ssize_t n;
	int st;

	if (pipe(fds))
		return SUB_FAIL;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = count_alarm;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGALRM, &sa, NULL))
		return SUB_FAIL;

	pid = fork();
	if (pid < 0)
		return SUB_FAIL;
	if (pid == 0) {
		struct timespec ts = { 0, 300 * 1000 * 1000 };

		/* Interval timers are not inherited across fork. */
		close(fds[0]);
		nanosleep(&ts, NULL);
		if (write(fds[1], msg, sizeof(msg)) != sizeof(msg))
			_exit(1);
		_exit(0);
	}
	close(fds[1]);

	alarms_seen = 0;
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 20 * 1000;
	it.it_value = it.it_interval;
	setitimer(ITIMER_REAL, &it, NULL);

	/*
	 * This read blocks ~300ms while SIGALRM fires every 20ms; each
	 * delivery interrupts and (SA_RESTART) re-executes the read.  A
	 * broken restart path turns it into a different syscall or makes
	 * it return garbage.
	 */
	n = read(fds[0], buf, sizeof(buf));

	memset(&it, 0, sizeof(it));
	setitimer(ITIMER_REAL, &it, NULL);
	close(fds[0]);
	waitpid(pid, &st, 0);

	if (n != (ssize_t)sizeof(msg) || memcmp(buf, msg, sizeof(msg)))
		return SUB_FAIL;
	if (alarms_seen < 3)	/* the storm never hit the read: no signal */
		return SUB_SKIP;
	return SUB_PASS;
}

static int sub_restart_nanosleep(void)
{
	struct timespec req = { 0, 400 * 1000 * 1000 };
	double t0, elapsed;
	pid_t pid;
	int r, st;

	/*
	 * The child exits mid-sleep; SIGCHLD's default disposition
	 * discards it, so the kernel wakes the sleep and resumes it via
	 * the restart block.  The sleep must still run to completion.
	 */
	signal(SIGCHLD, SIG_DFL);
	pid = fork();
	if (pid < 0)
		return SUB_FAIL;
	if (pid == 0) {
		struct timespec ts = { 0, 100 * 1000 * 1000 };

		nanosleep(&ts, NULL);
		_exit(0);
	}

	t0 = now();
	r = nanosleep(&req, NULL);
	elapsed = now() - t0;
	waitpid(pid, &st, 0);

	if (r != 0)
		return SUB_FAIL;
	if (elapsed < 0.38)
		return SUB_FAIL;
	return SUB_PASS;
}

/* ---- signal frame round trips ------------------------------------------ */

static void start_storm(long usec)
{
	struct itimerval it;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = count_alarm;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);

	alarms_seen = 0;
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = usec;
	it.it_value = it.it_interval;
	setitimer(ITIMER_REAL, &it, NULL);
}

static void stop_storm(void)
{
	struct itimerval it;

	memset(&it, 0, sizeof(it));
	setitimer(ITIMER_REAL, &it, NULL);
}

/*
 * The work under the storm and the control run must execute the very
 * same instructions for the very same iteration counts.  The storm run
 * goes first and works in chunks until enough signals have actually
 * hit it (how long that takes depends on the machine); the control
 * then replays the same number of chunks with the timer off.
 */
#define WORK_CHUNK	100000
#define WORK_MAX_CHUNKS	20000
#define WORK_MIN_HITS	5

static double fp_work(long chunks)
{
	double a = 1.0, b = 2.5, c = -0.75, d = 3.125;
	long k;
	int i;

	for (k = 0; k < chunks; k++) {
		for (i = 0; i < WORK_CHUNK; i++) {
			a = a * 1.0000001 + 0.1;
			b = b / 1.0000002 - 0.05;
			c = c + a * 0.001 - b * 0.002;
			d = d - c * 0.0005 + 0.025;
		}
	}
	return a + b + c + d;
}

static int sub_sig_fpstate(void)
{
	double with_signals = 0, control;
	long chunks = 0;

	start_storm(2000);
	while (alarms_seen < WORK_MIN_HITS && chunks < WORK_MAX_CHUNKS) {
		chunks += 100;
		with_signals = fp_work(100);
	}
	stop_storm();
	if (alarms_seen < WORK_MIN_HITS)
		return SUB_SKIP;

	/*
	 * Only the LAST 100-chunk block's result is compared; every
	 * block starts from the same constants, so one control block
	 * reproduces it exactly -- if signal frames preserve FP state.
	 */
	control = fp_work(100);
	if (memcmp(&with_signals, &control, sizeof(double)))
		return SUB_FAIL;
	return SUB_PASS;
}

static unsigned long int_work(long chunks)
{
	unsigned long a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8;
	long k;
	int i;

	for (k = 0; k < chunks; k++) {
		for (i = 0; i < WORK_CHUNK; i++) {
			a = a * 6364136223846793005UL + 1442695040888963407UL;
			b ^= a >> 17;
			c += b * 31;
			d = (d << 7) | (d >> 57);
			e ^= c + d;
			f = f * 2862933555777941757UL + 3037000493UL;
			g += e ^ f;
			h = (h >> 3) ^ g;
		}
	}
	return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h;
}

static int sub_sig_regs(void)
{
	unsigned long with_signals = 0, control;
	long chunks = 0;

	start_storm(2000);
	while (alarms_seen < WORK_MIN_HITS && chunks < WORK_MAX_CHUNKS) {
		chunks += 100;
		with_signals = int_work(100);
	}
	stop_storm();
	if (alarms_seen < WORK_MIN_HITS)
		return SUB_SKIP;

	control = int_work(100);
	if (with_signals != control)
		return SUB_FAIL;
	return SUB_PASS;
}

static void *altstack_base;
static volatile int altstack_ok;

static void altstack_handler(int sig)
{
	char probe;
	unsigned long sp = (unsigned long)&probe;
	unsigned long lo = (unsigned long)altstack_base;

	(void)sig;
	altstack_ok = (sp >= lo && sp < lo + SIGSTKSZ);
}

static int sub_sigaltstack(void)
{
	struct sigaction sa;
	stack_t st;

	altstack_base = malloc(SIGSTKSZ);
	if (!altstack_base)
		return SUB_FAIL;
	st.ss_sp = altstack_base;
	st.ss_flags = 0;
	st.ss_size = SIGSTKSZ;
	if (sigaltstack(&st, NULL))
		return SUB_FAIL;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = altstack_handler;
	sa.sa_flags = SA_ONSTACK;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL))
		return SUB_FAIL;

	altstack_ok = 0;
	raise(SIGUSR1);
	return altstack_ok ? SUB_PASS : SUB_FAIL;
}

static void noop_handler(int sig)
{
	(void)sig;
}

static int sub_sig_mask_restore(void)
{
	sigset_t set, old, after;

	/* Block SIGUSR2, then take a signal; sigreturn must restore the
	 * mask exactly from the frame. */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	if (sigprocmask(SIG_SETMASK, &set, &old))
		return SUB_FAIL;

	signal(SIGUSR1, noop_handler);
	raise(SIGUSR1);

	if (sigprocmask(SIG_SETMASK, NULL, &after))
		return SUB_FAIL;
	sigprocmask(SIG_SETMASK, &old, NULL);

	if (!sigismember(&after, SIGUSR2))
		return SUB_FAIL;
	if (sigismember(&after, SIGUSR1))
		return SUB_FAIL;
	return SUB_PASS;
}

/* ---- driver ------------------------------------------------------------- */

struct subtest {
	const char *name;
	int (*fn)(void);
};

static const struct subtest subtests[] = {
	{ "fork_wait",		sub_fork_wait },
	{ "fork_pipe",		sub_fork_pipe },
	{ "wait_signal",	sub_wait_signal },
	{ "execve",		sub_execve },
	{ "pagefault_pattern",	sub_pagefault_pattern },
	{ "cow",		sub_cow },
	{ "madv_dontneed",	sub_madv_dontneed },
	{ "mprotect_segv",	sub_mprotect_segv },
	{ "tls_fork",		sub_tls_fork },
	{ "tls_thread_fork",	sub_tls_thread_fork },
	{ "restart_read",	sub_restart_read },
	{ "restart_nanosleep",	sub_restart_nanosleep },
	{ "sig_fpstate",	sub_sig_fpstate },
	{ "sig_regs",		sub_sig_regs },
	{ "sigaltstack",	sub_sigaltstack },
	{ "sig_mask_restore",	sub_sig_mask_restore },
};

#define NSUBTESTS ((int)(sizeof(subtests) / sizeof(subtests[0])))

static void run_subtest(const struct subtest *t)
{
	int st, timed_out = 0;
	double deadline;
	pid_t pid, r;

	pid = fork();
	if (pid < 0) {
		um_res_fail(t->name, "fork of subtest runner failed");
		return;
	}
	if (pid == 0)
		_exit(t->fn());

	/*
	 * The watchdog lives in the parent: several subtests program
	 * ITIMER_REAL themselves, so an alarm() inside the child would
	 * be silently overwritten by the very code it is guarding.
	 */
	deadline = now() + SUBTEST_TIMEOUT;
	for (;;) {
		struct timespec ts = { 0, 20 * 1000 * 1000 };

		r = waitpid(pid, &st, WNOHANG);
		if (r == pid)
			break;
		if (r < 0) {
			um_res_fail(t->name, "waitpid failed");
			return;
		}
		if (now() > deadline) {
			kill(pid, SIGKILL);
			waitpid(pid, &st, 0);
			timed_out = 1;
			break;
		}
		nanosleep(&ts, NULL);
	}
	if (timed_out) {
		um_res_fail(t->name, "timeout");
		return;
	}
	if (WIFSIGNALED(st)) {
		um_res_fail(t->name, "killed by signal");
		return;
	}
	switch (WEXITSTATUS(st)) {
	case SUB_PASS:
		um_res_ok(t->name);
		break;
	case SUB_SKIP:
		um_res_skip(t->name, "prerequisite not met");
		break;
	default:
		um_res_fail(t->name, "subtest reported failure");
		break;
	}
}

int main(int argc, char **argv)
{
	int i;

	if (argc > 1 && strcmp(argv[1], "--exec-child") == 0) {
		const char *mark = getenv("UM_EXEC_MARK");

		return (mark && strcmp(mark, "um-selftest") == 0) ? 42 : 3;
	}

	self_exe = argv[0];
	um_guest_setup();
	if (um_guest_is_init)
		self_exe = (char *)"/proc/self/exe";

	um_result("PLAN %d", NSUBTESTS);
	for (i = 0; i < NSUBTESTS; i++)
		run_subtest(&subtests[i]);

	um_guest_done(0);
	return 0;
}
