/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared plumbing for UML guest test payloads.
 *
 * Each payload runs as init (PID 1) of a UML instance whose root is a
 * hostfs mount of a scratch directory on the host.  Results are reported
 * through two independent channels:
 *
 *   - a result file ("/result.tap") written through hostfs, which the
 *     host-side runner reads back after the UML instance exits.  This is
 *     the authoritative channel: it works even when no console is
 *     available, and a truncated file is evidence of a crash rather than
 *     a mysteriously quiet run.
 *   - the console, for humans reading the boot log.
 *
 * A payload signals completion by calling um_guest_done(), which writes a
 * final marker line and powers the machine off.  Powering off matters:
 * PID 1 exiting is a kernel panic ("Attempted to kill init!"), which with
 * panic=-1 is indistinguishable at the process level from a real crash.
 *
 * The same binaries can also run directly on the host (for native
 * baseline numbers, or for debugging).  PID 1 is the discriminator: only
 * as init do they mount /proc, write /result.tap and power off.
 *
 * Copyright (C) 2026 Fabian Franz
 */
#ifndef _UM_GUEST_H
#define _UM_GUEST_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <linux/reboot.h>

static int um_guest_is_init;
static int um_result_fd = -1;

static void um_result(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (n > (int)sizeof(buf) - 2)
		n = sizeof(buf) - 2;
	buf[n++] = '\n';
	buf[n] = '\0';

	/* Console copy for humans; result file for the runner. */
	fputs(buf, stdout);
	fflush(stdout);
	if (um_result_fd >= 0) {
		if (write(um_result_fd, buf, n) != n)
			fputs("# short write to /result.tap\n", stdout);
	}
}

static inline void um_res_ok(const char *name)
{
	um_result("ok %s", name);
}

static inline void um_res_fail(const char *name, const char *why)
{
	um_result("not ok %s # %s", name, why);
}

static inline void um_res_skip(const char *name, const char *why)
{
	um_result("skip %s # %s", name, why);
}

/*
 * Called first.  Detects init-ness, opens the result file and mounts
 * /proc (several tests and the fault counter need it; the runner
 * pre-creates the mount point in the scratch root).
 */
static void um_guest_setup(void)
{
	um_guest_is_init = (getpid() == 1);
	if (!um_guest_is_init)
		return;

	um_result_fd = open("/result.tap",
			    O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
	if (mount("proc", "/proc", "proc", 0, NULL) != 0)
		um_result("# mount /proc failed (some tests may skip)");
}

static void um_guest_done(int status)
{
	um_result("GUEST_DONE");
	if (!um_guest_is_init)
		_exit(status);
	if (um_result_fd >= 0)
		close(um_result_fd);
	sync();
	reboot(LINUX_REBOOT_CMD_POWER_OFF);
	/* Not reached under UML; be a well-behaved init regardless. */
	for (;;)
		pause();
}

#endif /* _UM_GUEST_H */
