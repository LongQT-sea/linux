#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <sys/fcntl.h>
#include <asm/unistd.h>
#include <sysdep/stub.h>
#include <stub-data.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <generated/asm-offsets.h>

static noinline void __attribute__((used)) real_init(void)
{
	struct stub_init_data init_data;
	unsigned long res;
	struct {
		void  *ss_sp;
		int    ss_flags;
		size_t ss_size;
	} stack = {
		.ss_size = STUB_DATA_PAGES * UM_KERN_PAGE_SIZE,
	};
	struct {
		void *sa_handler_;
		unsigned long sa_flags;
		void *sa_restorer;
		unsigned long long sa_mask;
	} sa = {
		/* Need to set SA_RESTORER (but the handler never returns) */
		.sa_flags = SA_ONSTACK | SA_NODEFER | SA_SIGINFO | UM_SA_RESTORER,
	};

	/* set a nice name */
	stub_syscall2(__NR_prctl, PR_SET_NAME, (unsigned long)"uml-userspace");

	/* Make sure this process dies if the kernel dies */
	stub_syscall2(__NR_prctl, PR_SET_PDEATHSIG, SIGKILL);

	/* Needed in SECCOMP mode (and safe to do anyway) */
	stub_syscall5(__NR_prctl, PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

	/* Read init_data from STDIN. SOCK_STREAM may deliver short reads
	 * (observed: 18-byte first segment), so loop until the full
	 * struct arrived or the writer closed. */
	{
		char *buf = (char *)&init_data;
		size_t got = 0;

		while (got < sizeof(init_data)) {
			res = stub_syscall3(__NR_read, 0,
					    (unsigned long)(buf + got),
					    sizeof(init_data) - got);
			if ((long)res <= 0)
				break;
			got += res;
		}
		if (got != sizeof(init_data)) {
			/* Encode read errno/short-read in the exit code
			 * for post-mortem via the stub-death path. */
			int code = 100 + (int)(((res < 0 ? -res : res) |
						got) & 0x3f);
			stub_syscall1(__NR_exit, code);
		}
	}

	/* In SECCOMP mode, FD 0 is a socket and is later used for FD passing */
	if (!init_data.seccomp)
		stub_syscall1(__NR_close, 0);
	else
		stub_syscall3(__NR_fcntl, 0, F_SETFL, O_NONBLOCK);

	/*
	 * Whatever this architecture has to settle in a fresh stub,
	 * before the seccomp filter is installed and so still able to
	 * make any syscall.
	 */
	stub_arch_init(init_data.arch_flags);

	/* map stub code + data */
	STUB_MMAP_CALL(res,
		       init_data.stub_start, UM_KERN_PAGE_SIZE,
		       PROT_READ | PROT_EXEC, MAP_FIXED | MAP_SHARED,
		       init_data.stub_code_fd, init_data.stub_code_offset);
	if (res != init_data.stub_start)
		stub_syscall1(__NR_exit, 11);

	STUB_MMAP_CALL(res,
		       init_data.stub_start + UM_KERN_PAGE_SIZE,
		       STUB_DATA_PAGES * UM_KERN_PAGE_SIZE,
		       PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED,
		       init_data.stub_data_fd, init_data.stub_data_offset);
	if (res != init_data.stub_start + UM_KERN_PAGE_SIZE)
		stub_syscall1(__NR_exit, 12);

	/* In SECCOMP mode, we only need the signalling FD from now on */
	if (init_data.seccomp) {
		res = stub_syscall3(__NR_close_range, 1, ~0U, 0);
		if (res != 0)
			stub_syscall1(__NR_exit, 13);
	}

	/* setup signal stack inside stub data */
	stack.ss_sp = (void *)init_data.stub_start + UM_KERN_PAGE_SIZE;
	stub_syscall2(__NR_sigaltstack, (unsigned long)&stack, 0);

	/* register signal handlers */
	sa.sa_handler_ = (void *) init_data.signal_handler;
	sa.sa_restorer = (void *) init_data.signal_restorer;
	if (!init_data.seccomp) {
		/* In ptrace mode, the SIGSEGV handler never returns */
		sa.sa_mask = 0;

		res = stub_syscall4(__NR_rt_sigaction, SIGSEGV,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 14);
	} else {
		/* SECCOMP mode uses rt_sigreturn, need to mask all signals */
		sa.sa_mask = ~0ULL;

		res = stub_syscall4(__NR_rt_sigaction, SIGSEGV,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 15);

		res = stub_syscall4(__NR_rt_sigaction, SIGSYS,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 16);

		res = stub_syscall4(__NR_rt_sigaction, SIGALRM,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 17);

		res = stub_syscall4(__NR_rt_sigaction, SIGTRAP,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 18);

		res = stub_syscall4(__NR_rt_sigaction, SIGILL,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 19);

		res = stub_syscall4(__NR_rt_sigaction, SIGFPE,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 20);
	}

	/*
	 * If in seccomp mode, install the SECCOMP filter and trigger a syscall.
	 * Otherwise set PTRACE_TRACEME and do a SIGSTOP.
	 */
	if (init_data.seccomp) {
		struct sock_filter filter[] = {
			/*
			 * Accept only syscalls whose instruction pointer
			 * lies inside the whole stub region [stub_start,
			 * stub_start + STUB_SIZE): the CODE page (guest
			 * syscall replay) AND the DATA page(s) — the
			 * handler runs on the sigaltstack there and
			 * issues futex/recvmsg itself. Anything outside:
			 * guest syscall → TRAP. Wrong arch → KILL.
			 */
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				/* s390x is BIG-endian: bytes [0..3] of the
				 * 8-byte ip are the HIGH half */
				 offsetof(struct seccomp_data, instruction_pointer)),
			/* upper32 != stub_start>>32 → guest syscall (TRAP) */
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
				 (init_data.stub_start) >> 32, 0, 6),

			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				/* BE: bytes [4..7] are the LOW half */
				 (offsetof(struct seccomp_data, instruction_pointer) + 4)),
			/* ip < start → TRAP ; ip >= end → TRAP */
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K,
				 (init_data.stub_start) & 0xffffffff, 0, 4),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K,
				 ((init_data.stub_start + STUB_SIZE) & 0xffffffff), 3, 0),


			/* Inside the stub window: verify architecture */
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 offsetof(struct seccomp_data, arch)),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
				 UM_SECCOMP_ARCH_NATIVE, 2, 0),
			BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),

			/* Guest syscall: relay via SIGSYS */
			BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),

			/* Load syscall number */
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 offsetof(struct seccomp_data, nr)),
			/*
			 * Permitted syscalls (issued by the handler
			 * inside the stub window). Each match jumps to
			 * the final RET ALLOW; the implicit
			 * fall-through of a non-match MUST hit the
			 * RET TRAP below — without it every jump
			 * target is out of range (EINVAL at install)
			 * and unmatched in-window syscalls would be
			 * silently allowed.
			 */
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_futex, 7, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_recvmsg, 6, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_close, 5, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, STUB_MMAP_NR, 4, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_munmap, 3, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 173 /* rt_sigreturn */, 2, 0),
#ifdef CONFIG_UML_S390
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_ptrace /* TRACEME before first relay */, 1, 0),
#endif
			/* Non-allowlisted syscall from inside the
			 * stub window: trap it like a guest syscall. */
			BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),

			BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		};
		struct sock_fprog prog = {
			.len = sizeof(filter) / sizeof(filter[0]),
			.filter = filter,
		};

#ifdef CONFIG_UML_S390
	/*
	 * Become traced by the parent BEFORE the filter goes live: any
	 * syscall issued after the filter (including this one, from the
	 * stub_exe mapping outside the stub IP window) traps and is
	 * RELAYED to UML, which would execute ptrace(TRACEME) on
	 * ITSELF and hand us a meaningless 0 (box-proven: TracerPid
	 * stayed 0 while the relay worked). Pre-filter, the svc runs
	 * for real and the tracer sees every SIGSYS delivery-stop.
	 */
	{
		long tr = stub_syscall4(__NR_ptrace,
					0 /* PTRACE_TRACEME */,
					0, 0, 0);
		if (tr != 0)
			stub_syscall2(__NR_exit_group, 90 + tr, 0);
	}
#endif

		if (stub_syscall3(__NR_seccomp, SECCOMP_SET_MODE_FILTER,
				  SECCOMP_FILTER_FLAG_TSYNC,
				  (unsigned long)&prog) != 0)
			stub_syscall1(__NR_exit, 21);

		/* Fall through: the exit syscall hits the filter and
		 * raises SIGSYS, entering stub_signal_interrupt — the
		 * first relay round. */
	} else {
		stub_syscall4(__NR_ptrace, PTRACE_TRACEME, 0, 0, 0);

		stub_syscall2(__NR_kill, stub_syscall0(__NR_getpid), SIGSTOP);
	}

	/* In seccomp mode NOTREACHED (the filtered exit above raised
	 * SIGSYS); ptrace mode stops here after its SIGSTOP. */
	stub_syscall1(__NR_exit, 30);

	__builtin_unreachable();
}

/*
 * Since the stack after exec() starts at the top-most address, but
 * that is exactly where the stub code and data must be mapped, the
 * entry point must:
 *  - push the stack down by 1 code and STUB_DATA_PAGES data pages
 *  - call real_init()
 * This way real_init() can use the stack normally, while the original
 * stack further up (at higher addresses) becomes inaccessible after
 * the mmap() calls it makes.
 *
 * A naked C function is not a reliable container for these
 * instructions: only some GCC targets honor the attribute (aarch64
 * GCC warns and ignores it), and an ignored attribute means a
 * compiler-emitted prologue touches the stack before it is moved.
 * Each backend provides the entry as real assembly (STUB_EXE_START
 * in sysdep/stub.h).
 */
__asm__(
	".pushsection .text\n"
	".global _start\n"
	"_start:\n"
	STUB_EXE_START
	".popsection\n"
);
