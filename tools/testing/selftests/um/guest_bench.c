// SPDX-License-Identifier: GPL-2.0
/*
 * What does a UML guest actually cost?
 *
 * "UML runs at native speed" is half true and the half that is false is
 * the half people notice.  Guest *computation* runs natively -- the
 * instructions execute on the CPU with nothing in between.  Guest
 * *kernel entries* do not: every syscall and every page fault has to be
 * intercepted and serviced by the UML kernel, and that interception is
 * the whole cost.  So measure the parts separately rather than timing
 * "a shell" and guessing:
 *
 *   compute   pure userspace arithmetic, no kernel entry at all.  This
 *	       should be indistinguishable from native, which makes it
 *	       the run's validator: if compute moves between rounds or
 *	       between conditions, the machine moved, not the kernel,
 *	       and the other numbers mean nothing.
 *   syscall   getppid() in a loop -- the cheapest syscall there is, so
 *	       the measurement is almost entirely interception overhead.
 *   openat    a path-taking syscall, because a cheap-syscall-only
 *	       comparison flatters interception schemes that pass most
 *	       calls through untouched.
 *   fault     touching fresh anonymous pages: in ptrace mode each one
 *	       is a SIGSEGV to the tracer and a round trip; in seccomp
 *	       mode the path is shorter.  Reported with faults/page from
 *	       /proc/self/stat, because "slow" and "took no faults at
 *	       all" need different fixes and the timing alone cannot
 *	       tell them apart.
 *   populate  the same allocation via MAP_POPULATE: identical work,
 *	       zero per-page interception.  fault - populate is what an
 *	       intercepted fault costs over the work it does.
 *   refault   MADV_DONTNEED then touch again: the VMA and page tables
 *	       already exist, isolating first-touch setup cost.
 *   store     writing every byte of already-present memory: the floor.
 *	       No fault can beat the cost of writing the page's bytes.
 *   forkexit  fork() + _exit() + wait(): a new guest mm and stub.
 *   forkexec  fork() + execve() + wait(): the compound case a shell
 *	       spends its life doing (this binary re-executes itself
 *	       with --true, so no shell utilities are needed).
 *
 * Arms run interleaved, round-robin, several rounds per boot, and the
 * report is medians with the min..max spread printed beside them: a
 * difference between two arms measured seconds apart cannot be blamed
 * on thermal drift or background load, and a spread wider than the
 * effect voids the comparison honestly.  One un-recorded warm-up round
 * absorbs first-touch costs (allocator growth, binary paging).
 *
 * Runs as init inside UML (results land in /result.tap via hostfs) or
 * directly on the host for a native baseline.  BENCH_ROUNDS on the UML
 * command line reaches this payload through the environment: UML hands
 * unrecognised key=value boot arguments to init.
 *
 * Derived from perfbench.c and faultbench.c in the um-arm64 harness.
 *
 * Copyright (C) 2026 Oleksii Zakharov <contact@zalexdev.com>
 * Copyright (C) 2026 Fabian Franz
 */
#include "um_guest.h"

#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#define MAX_ROUNDS 32

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/*
 * Minor faults taken by this process, from /proc/self/stat field 10.
 * Field 2 is the comm, is parenthesised, and may itself contain spaces
 * and ')', so the last ')' in the line ends it -- everything before is
 * fields 1 and 2, and each space from there starts the next field.
 */
static unsigned long minflt(void)
{
	char buf[1024], *s;
	unsigned long v = 0;
	ssize_t n;
	int fd, field;

	fd = open("/proc/self/stat", O_RDONLY);
	if (fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = '\0';

	s = strrchr(buf, ')');
	if (!s)
		return 0;
	for (s++, field = 2; *s; s++) {
		if (*s != ' ')
			continue;
		if (++field == 10) {
			v = strtoul(s + 1, NULL, 10);
			break;
		}
	}
	return v;
}

static char *self_exe;
static long pagesz;

/* Loop sizes: one round stays around a second inside a guest. */
static long n_compute = 20 * 1000 * 1000;
static long n_syscall = 50000;
static long n_openat = 5000;
static long n_pages = 4096;
static long n_forkexit = 200;
static long n_forkexec = 100;

/* Results: us/op per arm per round, plus faults/page for the fault arm. */
struct arm {
	const char *name;
	double us[MAX_ROUNDS];
	double faults_pp[MAX_ROUNDS];
	int has_faults;
};

static struct arm arms[] = {
	{ .name = "compute" },
	{ .name = "syscall" },
	{ .name = "openat" },
	{ .name = "fault", .has_faults = 1 },
	{ .name = "populate", .has_faults = 1 },
	{ .name = "refault", .has_faults = 1 },
	{ .name = "store" },
	{ .name = "forkexit" },
	{ .name = "forkexec" },
};

#define NARMS ((int)(sizeof(arms) / sizeof(arms[0])))

enum {
	A_COMPUTE, A_SYSCALL, A_OPENAT, A_FAULT, A_POPULATE, A_REFAULT,
	A_STORE, A_FORKEXIT, A_FORKEXEC
};

/* Deliberately not optimisable away: the checksum is printed once. */
static unsigned long compute_chk;

static void arm_compute(int round)
{
	unsigned long acc = 12345;
	double t0 = now();
	long i;

	for (i = 0; i < n_compute; i++)
		acc = acc * 6364136223846793005UL + 1442695040888963407UL;
	arms[A_COMPUTE].us[round] = (now() - t0) * 1e6 / n_compute;
	compute_chk = acc;
}

static void arm_syscall(int round)
{
	double t0 = now();
	long i;

	for (i = 0; i < n_syscall; i++)
		syscall(SYS_getppid);
	arms[A_SYSCALL].us[round] = (now() - t0) * 1e6 / n_syscall;
}

static void arm_openat(int round)
{
	double t0 = now();
	long i;

	for (i = 0; i < n_openat; i++) {
		int fd = open(self_exe, O_RDONLY);

		if (fd < 0) {
			arms[A_OPENAT].us[round] = -1;
			return;
		}
		close(fd);
	}
	arms[A_OPENAT].us[round] = (now() - t0) * 1e6 / n_openat;
}

static void touch_all(volatile char *p)
{
	long i;

	for (i = 0; i < n_pages; i++)
		p[i * pagesz] = 1;
}

/*
 * fault, refault, store and populate share one round so their inputs
 * (same region, same size) are identical; each records its own row.
 */
static void arm_fault_group(int round)
{
	size_t len = (size_t)n_pages * pagesz;
	unsigned long f0;
	volatile char *p;
	double t0;
	long i;

	p = mmap(NULL, len, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == (volatile char *)MAP_FAILED) {
		arms[A_FAULT].us[round] = -1;
		arms[A_REFAULT].us[round] = -1;
		arms[A_STORE].us[round] = -1;
		arms[A_POPULATE].us[round] = -1;
		return;
	}

	f0 = minflt();
	t0 = now();
	touch_all(p);
	arms[A_FAULT].us[round] = (now() - t0) * 1e6 / n_pages;
	arms[A_FAULT].faults_pp[round] = (double)(minflt() - f0) / n_pages;

	/* The floor: same pages, now present, plain stores. */
	t0 = now();
	for (i = 0; i < (long)(len / sizeof(long)); i++)
		((volatile long *)p)[i] = 0x0101010101010101L;
	arms[A_STORE].us[round] = (now() - t0) * 1e6 / n_pages;

	if (madvise((void *)p, len, MADV_DONTNEED) == 0) {
		f0 = minflt();
		t0 = now();
		touch_all(p);
		arms[A_REFAULT].us[round] = (now() - t0) * 1e6 / n_pages;
		arms[A_REFAULT].faults_pp[round] =
			(double)(minflt() - f0) / n_pages;
	} else {
		arms[A_REFAULT].us[round] = -1;
	}
	munmap((void *)p, len);

	f0 = minflt();
	t0 = now();
	p = mmap(NULL, len, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if (p == (volatile char *)MAP_FAILED) {
		arms[A_POPULATE].us[round] = -1;
		return;
	}
	arms[A_POPULATE].us[round] = (now() - t0) * 1e6 / n_pages;
	arms[A_POPULATE].faults_pp[round] =
		(double)(minflt() - f0) / n_pages;
	munmap((void *)p, len);
}

static void arm_forkexit(int round)
{
	double t0 = now();
	long i;

	for (i = 0; i < n_forkexit; i++) {
		pid_t pid = fork();
		int st;

		if (pid == 0)
			_exit(0);
		if (pid < 0) {
			arms[A_FORKEXIT].us[round] = -1;
			return;
		}
		waitpid(pid, &st, 0);
	}
	arms[A_FORKEXIT].us[round] = (now() - t0) * 1e6 / n_forkexit;
}

static void arm_forkexec(int round)
{
	double t0 = now();
	long i;

	for (i = 0; i < n_forkexec; i++) {
		pid_t pid = fork();
		int st;

		if (pid == 0) {
			char *argvv[] = { self_exe, (char *)"--true", NULL };

			execv(self_exe, argvv);
			_exit(127);
		}
		if (pid < 0) {
			arms[A_FORKEXEC].us[round] = -1;
			return;
		}
		waitpid(pid, &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
			arms[A_FORKEXEC].us[round] = -1;
			return;
		}
	}
	arms[A_FORKEXEC].us[round] = (now() - t0) * 1e6 / n_forkexec;
}

static void run_round(int round)
{
	arm_compute(round);
	arm_syscall(round);
	arm_openat(round);
	arm_fault_group(round);
	arm_forkexit(round);
	arm_forkexec(round);
}

static int cmp_double(const void *a, const void *b)
{
	double da = *(const double *)a, db = *(const double *)b;

	return (da > db) - (da < db);
}

/* Median over the valid (>= 0) entries; -1 when none are. */
static double median(const double *v, int n, double *lo, double *hi)
{
	double s[MAX_ROUNDS];
	int i, m = 0;

	for (i = 0; i < n; i++)
		if (v[i] >= 0)
			s[m++] = v[i];
	if (!m)
		return -1;
	qsort(s, m, sizeof(s[0]), cmp_double);
	if (lo)
		*lo = s[0];
	if (hi)
		*hi = s[m - 1];
	return m & 1 ? s[m / 2] : (s[m / 2 - 1] + s[m / 2]) / 2;
}

int main(int argc, char **argv)
{
	int rounds = 3;
	const char *e;
	int r, a;

	if (argc > 1 && strcmp(argv[1], "--true") == 0)
		return 0;

	self_exe = argv[0];
	um_guest_setup();
	if (um_guest_is_init)
		self_exe = (char *)"/proc/self/exe";

	e = getenv("BENCH_ROUNDS");
	if (e)
		rounds = atoi(e);
	if (rounds < 1)
		rounds = 1;
	if (rounds > MAX_ROUNDS)
		rounds = MAX_ROUNDS;

	pagesz = sysconf(_SC_PAGESIZE);

	um_result("PLAN 1");
	um_result("# bench pagesz=%ld rounds=%d pages=%ld", pagesz, rounds,
		  n_pages);

	/* Warm-up round, not recorded. */
	run_round(0);

	for (r = 0; r < rounds; r++) {
		run_round(r);
		for (a = 0; a < NARMS; a++) {
			if (arms[a].has_faults && arms[a].us[r] >= 0)
				um_result("BENCHROW round=%d arm=%s us=%.3f faults_pp=%.2f",
					  r, arms[a].name, arms[a].us[r],
					  arms[a].faults_pp[r]);
			else
				um_result("BENCHROW round=%d arm=%s us=%.3f",
					  r, arms[a].name, arms[a].us[r]);
		}
	}

	for (a = 0; a < NARMS; a++) {
		double lo = 0, hi = 0;
		double med = median(arms[a].us, rounds, &lo, &hi);

		if (med < 0)
			um_result("BENCHMED arm=%s unavailable", arms[a].name);
		else
			um_result("BENCHMED arm=%s median_us=%.3f min=%.3f max=%.3f n=%d",
				  arms[a].name, med, lo, hi, rounds);
	}

	/*
	 * The validator: compute must not move.  If it does, the box was
	 * busy or throttling and every comparison above is suspect.
	 */
	{
		double lo = 0, hi = 0;
		double med = median(arms[A_COMPUTE].us, rounds, &lo, &hi);

		if (med > 0 && (hi - lo) / med > 0.10)
			um_result("BENCH_UNSTABLE compute spread %.1f%% exceeds 10%%",
				  (hi - lo) / med * 100);
		else
			um_result("BENCH_STABLE");
	}
	um_result("# compute checksum %lu", compute_chk);

	um_res_ok("bench_completed");
	um_guest_done(0);
	return 0;
}
