// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Benjamin Berg <benjamin@sipsolutions.net>
 */

#include <sysdep/stub.h>
#include <linux/sched.h>

#include <linux/futex.h>
#include <sys/socket.h>
#include <errno.h>

/*
 * Known security issues
 *
 * Userspace can jump to this address to execute *any* syscall that is
 * permitted by the stub. As we will return afterwards, it can do
 * whatever it likes, including:
 * - Tricking the kernel into handing out the memory FD
 * - Using this memory FD to read/write all physical memory
 * - Running in parallel to the kernel processing a syscall
 *   (possibly creating data races?)
 * - Blocking e.g. SIGALRM to avoid time based scheduling
 *
 * To avoid this, the permitted location for each syscall needs to be
 * checked for in the SECCOMP filter (which is reasonably simple). Also,
 * more care will need to go into considerations how the code might be
 * tricked by using a prepared stack (or even modifying the stack from
 * another thread in case SMP support is added).
 *
 * As for the SIGALRM, the best counter measure will be to check in the
 * kernel that the process is reporting back the SIGALRM in a timely
 * fashion.
 */
static __always_inline int syscall_handler(int fd_map[STUB_MAX_FDS])
{
	struct stub_data *d = get_stub_data();
	int i;
	unsigned long res;
	int fd;

	for (i = 0; i < d->syscall_data_len; i++) {
		struct stub_syscall *sc = &d->syscall_data[i];

		switch (sc->syscall) {
		case STUB_SYSCALL_MMAP:
			if (fd_map)
				fd = fd_map[sc->mem.fd];
			else
				fd = sc->mem.fd;

			STUB_MMAP_CALL(res, sc->mem.addr, sc->mem.length,
				       sc->mem.prot,
				       MAP_SHARED | MAP_FIXED,
				       fd, sc->mem.offset);
			if (res != sc->mem.addr) {
				d->err = res;
				d->syscall_data_len = i;
				return -1;
			}
			break;
		case STUB_SYSCALL_MUNMAP:
			res = stub_syscall2(__NR_munmap,
					    sc->mem.addr, sc->mem.length);
			if (res) {
				d->err = res;
				d->syscall_data_len = i;
				return -1;
			}
			break;
#ifdef CONFIG_UML_MT_STUB
		case STUB_SYSCALL_CLONE:
			/*
			 * New thread whose data area is at mem.addr; it starts
			 * on the top of its own sigstack.
			 */
			res = stub_clone_thread(STUB_CLONE_FLAGS,
						sc->mem.addr + sc->mem.length,
						stub_worker_start);
			if ((long)res < 0) {
				d->err = res;
				d->syscall_data_len = i;
				return -1;
			}
			break;
#endif
		default:
			d->err = -95; /* EOPNOTSUPP */
			d->syscall_data_len = i;
			return -1;
		}
	}

	d->err = 0;
	d->syscall_data_len = 0;

	return 0;
}

void __section(".__syscall_stub")
stub_syscall_handler(void)
{
	syscall_handler(NULL);

	trap_myself();
}

void __section(".__syscall_stub")
stub_signal_interrupt(int sig, siginfo_t *info, void *p)
{
	struct stub_data *d = get_stub_data();
	char rcv_data;
	union {
		char data[CMSG_SPACE(sizeof(int) * STUB_MAX_FDS)];
		struct cmsghdr align;
	} ctrl = {};
	struct iovec iov = {
		.iov_base = &rcv_data,
		.iov_len = 1,
	};
	struct msghdr msghdr = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = &ctrl,
		.msg_controllen = sizeof(ctrl),
	};
	ucontext_t *uc = p;
	struct cmsghdr *fd_msg;
	int *fd_map;
	int num_fds;
	long res;

	d->signal = sig;
	d->si_offset = (unsigned long)info - (unsigned long)&d->sigstack[0];
	d->mctx_offset = (unsigned long)&uc->uc_mcontext - (unsigned long)&d->sigstack[0];

	/* Save arch state the kernel cannot observe (arm64: TPIDR_EL0) */
	stub_seccomp_save_state(&d->arch_data);

restart_wait:
	/*
	 * The kernel runs on other host CPUs and may read the futex word
	 * without sleeping: hand over everything written so far (signal
	 * frame, offsets, results) with a release store, and read what the
	 * kernel wrote for us only after an acquire load.
	 */
	__atomic_store_n(&d->futex, FUTEX_IN_KERN, __ATOMIC_RELEASE);
	do {
		res = stub_syscall3(__NR_futex, (unsigned long)&d->futex,
				    FUTEX_WAKE, 1);
	} while (res == -EINTR);

	do {
		res = stub_syscall4(__NR_futex, (unsigned long)&d->futex,
				    FUTEX_WAIT, FUTEX_IN_KERN, 0);
	} while (res == -EINTR ||
		 __atomic_load_n(&d->futex, __ATOMIC_ACQUIRE) == FUTEX_IN_KERN);

	if (res < 0 && res != -EAGAIN)
		stub_syscall1(__NR_exit_group, 1);

	if (d->syscall_data_len) {
		/* Read passed FDs (if any) */
		do {
			res = stub_syscall3(__NR_recvmsg, 0, (unsigned long)&msghdr, 0);
		} while (res == -EINTR);

		/* We should never have a receive error (other than -EAGAIN) */
		if (res < 0 && res != -EAGAIN)
			stub_syscall1(__NR_exit_group, 1);

		/* Receive the FDs */
		num_fds = 0;
		fd_msg = msghdr.msg_control;
		fd_map = (void *)CMSG_DATA(fd_msg);
		if (res == iov.iov_len && msghdr.msg_controllen > sizeof(struct cmsghdr))
			num_fds = (fd_msg->cmsg_len - CMSG_LEN(0)) / sizeof(int);

		/* Try running queued syscalls. */
		res = syscall_handler(fd_map);

		while (num_fds)
			stub_syscall2(__NR_close, fd_map[--num_fds], 0);
	} else {
		res = 0;
	}

	if (res < 0 || d->restart_wait) {
		/* Report SIGSYS if we restart. */
		d->signal = SIGSYS;
		d->restart_wait = 0;

		goto restart_wait;
	}

	/* The kernel waits for the queued syscalls before letting others in */
	if (d->ack_syscalls) {
		d->ack_syscalls = 0;
		__atomic_store_n(&d->futex, FUTEX_SYSCALLS_DONE,
				 __ATOMIC_RELEASE);
		do {
			res = stub_syscall3(__NR_futex, (unsigned long)&d->futex,
					    FUTEX_WAKE, 1);
		} while (res == -EINTR);
	}

	/* Restore arch dependent state that is not part of the mcontext */
	stub_seccomp_restore_state(&d->arch_data);

	/* Return so that the host modified mcontext is restored. */
	stub_signal_return(info);
}

#ifdef CONFIG_UML_MT_STUB
/*
 * Entry point of a thread created by STUB_SYSCALL_CLONE. It runs on its own
 * sigstack with all signals blocked (inherited from the creating thread,
 * which is in its signal handler). Set up the sigaltstack, unblock signals
 * and trap, so the kernel finds it waiting in stub_signal_interrupt() with a
 * signal frame it can fill in to run userspace.
 */
void __section(".__syscall_stub") __noreturn
stub_worker_start(void)
{
	struct stub_data *d = get_stub_data();
	struct {
		void  *ss_sp;
		int    ss_flags;
		size_t ss_size;
	} stack = {
		.ss_sp = d->sigstack,
		.ss_size = sizeof(d->sigstack),
	};
	unsigned long long mask = 0;

	d->tid = stub_syscall0(__NR_gettid);
	/* Tell the kernel if this fails, the thread is unusable then */
	if (stub_syscall2(__NR_sigaltstack, (unsigned long)&stack, 0))
		d->tid = -1;
	stub_syscall4(__NR_rt_sigprocmask, SIG_SETMASK, (unsigned long)&mask,
		      0, sizeof(mask));

	for (;;)
		trap_myself();
}
#endif

void __section(".__syscall_stub")
stub_signal_restorer(void)
{
	/* We must not have anything on the stack when doing rt_sigreturn */
	stub_syscall0(__NR_rt_sigreturn);
}
