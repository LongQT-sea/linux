// SPDX-License-Identifier: GPL-2.0
/*
 * Decide, once per boot, what to do about the host's pointer
 * authentication keys in stub processes.
 *
 * Every stub is a fresh execve(), so the host gives it fresh PAC keys;
 * a guest fork()'s child then authenticates parent-signed return
 * addresses against unrelated keys and FPAC-faults (glibc's _Fork:
 * paciasp / clone / autiasp). Two remedies exist and which one is safe
 * depends on the host:
 *
 *  - Disable the keys in the stub (PR_PAC_SET_ENABLED_KEYS = 0). On a
 *    host that then executes the HINT-space PAC instructions as NOPs
 *    this is the clean fix: the port does not advertise
 *    HWCAP_PACA/PACG, so nothing should be signing anyway.
 *
 *  - On a host that instead traps PAC instructions whose key is
 *    disabled (observed on a 6.8 host under qemu -cpu max), disabling
 *    the keys would turn every paciasp/autiasp pair in a
 *    branch-protected guest userland into a SIGILL storm. There the
 *    keys must stay enabled: same-process sign/authenticate pairs then
 *    match, and only the cross-stub cases fault into the SIGILL
 *    emulation (arch_sigill_fixup() in arch/arm64/um/signal.c).
 *
 * Probe which kind of host this is with one sacrificial child: disable
 * its keys, run paciasp/autiasp, and see whether it survives. The
 * SIGILL emulation stays armed in both cases; retaa and friends are
 * not HINT-space encodings and trap with a disabled key even on hosts
 * that NOP the hints.
 *
 * The child is a clone(CLONE_VM) on its own small stack, the same
 * shape run_helper() uses, and for the same reason: by initcall time
 * this code runs on a kernel thread stack, which lives in UML's
 * physical memory, and physical memory is one MAP_SHARED file mapping.
 * A plain fork()'s child would run on the very same backing pages as
 * its parent's kernel stack and shred the parent's saved frames from
 * underneath it (the parent then returns to a zeroed x30 and panics
 * with "Segfault with no mm" at pc 0). CLONE_VM sidesteps that whole
 * class: one address space, a dedicated stack, nothing aliased.
 *
 * An __initcall is early enough: the first stub process is exec'd for
 * init's mm, after the initcalls have run.
 */
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <init.h>
#include <kern_util.h>
#include <os.h>
#include <skas.h>
#include <sysdep/stub-data.h>

#ifndef PR_PAC_SET_ENABLED_KEYS
#define PR_PAC_SET_ENABLED_KEYS	60
#define PR_PAC_APIAKEY		(1UL << 0)
#define PR_PAC_APIBKEY		(1UL << 1)
#define PR_PAC_APDAKEY		(1UL << 2)
#define PR_PAC_APDBKEY		(1UL << 3)
#endif

#define PAC_ALL_KEYS (PR_PAC_APIAKEY | PR_PAC_APIBKEY | \
		      PR_PAC_APDAKEY | PR_PAC_APDBKEY)

static int __init pac_probe_child(void *unused)
{
	/* Own process, own signal table: the parent keeps its handler. */
	signal(SIGILL, SIG_DFL);

	if (prctl(PR_PAC_SET_ENABLED_KEYS, PAC_ALL_KEYS, 0, 0, 0))
		_exit(2);

	/*
	 * paciasp (hint #25) then autiasp (hint #29), spelled as hints
	 * so no assembler extension is needed. With the keys disabled
	 * this pair either does nothing or kills this child with
	 * SIGILL, which is exactly the question.
	 */
	__asm__ volatile("hint #25\n\thint #29" ::: "x30", "memory");
	_exit(0);
}

static int __init os_probe_pac(void)
{
	unsigned long stack;
	int pid, err, status;

	stack = alloc_stack(0, 0);
	if (stack == 0) {
		os_info("PAC: no stack for the probe, keeping host keys enabled\n");
		return 0;
	}

	pid = clone(pac_probe_child, (void *)(stack + UM_KERN_PAGE_SIZE),
		    CLONE_VM, NULL);
	if (pid < 0) {
		os_info("PAC: probe clone failed, keeping host keys enabled\n");
		free_stack(stack, 0);
		return 0;
	}

	CATCH_EINTR(err = waitpid(pid, &status, __WALL));
	free_stack(stack, 0);
	if (err < 0) {
		os_info("PAC: probe wait failed, keeping host keys enabled\n");
		return 0;
	}

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		stub_arch_init_flags |= STUB_INIT_PAC_OFF;
		os_info("PAC: disabling host keys in stub processes\n");
	} else if (WIFEXITED(status) && WEXITSTATUS(status) == 2) {
		os_info("PAC: host has no PAC prctl; nothing to disable\n");
	} else if (WIFSIGNALED(status) && WTERMSIG(status) == SIGILL) {
		os_info("PAC: host traps disabled-key PAC; keeping keys enabled, relying on SIGILL emulation\n");
	} else {
		os_info("PAC: probe died with status 0x%x, keeping host keys enabled\n",
			status);
	}

	return 0;
}
__initcall(os_probe_pac);
