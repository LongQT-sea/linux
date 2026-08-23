// SPDX-License-Identifier: GPL-2.0
/*
 * In-guest module load/unload test for UML.
 *
 * The runner copies a .ko built against the kernel under test into the
 * guest root as /test_module.ko (CONFIG_TEST_LKM=m produces a suitable
 * one, see the suite's config fragment).  This payload loads it with
 * finit_module(2), confirms it appears in /proc/modules, unloads it
 * with delete_module(2) and confirms it is gone.
 *
 * The module's name is taken from the /proc/modules delta rather than
 * from the file name: the runner renames whatever module it was given,
 * and a module's name comes from its modinfo, not its path.
 *
 * Skips (rather than fails) when the kernel has no module support or
 * when no module file was provided, so the suite stays green on
 * CONFIG_MODULES=n configurations.
 *
 * Copyright (C) 2026 Fabian Franz
 */
#include "um_guest.h"

#include <stdlib.h>
#include <errno.h>
#include <sys/syscall.h>

#define MODPATH "/test_module.ko"
#define MAX_MODS 512

struct modlist {
	char names[MAX_MODS][64];
	int n;
};

static int read_modules(struct modlist *ml)
{
	char line[512];
	FILE *f;

	ml->n = 0;
	f = fopen("/proc/modules", "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f) && ml->n < MAX_MODS) {
		char *sp = strchr(line, ' ');

		if (!sp)
			continue;
		*sp = '\0';
		snprintf(ml->names[ml->n++], sizeof(ml->names[0]), "%.63s", line);
	}
	fclose(f);
	return 0;
}

static int listed(const struct modlist *ml, const char *name)
{
	int i;

	for (i = 0; i < ml->n; i++)
		if (strcmp(ml->names[i], name) == 0)
			return 1;
	return 0;
}

static void skip_all(const char *why)
{
	um_res_skip("module_load", why);
	um_res_skip("module_listed", why);
	um_res_skip("module_unload", why);
	um_guest_done(0);
}

int main(void)
{
	static struct modlist before, after;
	const char *newmod = NULL;
	int fd, i;

	um_guest_setup();
	um_result("PLAN 3");

	fd = open(MODPATH, O_RDONLY);
	if (fd < 0)
		skip_all("no /test_module.ko provided");

	if (read_modules(&before))
		skip_all("cannot read /proc/modules");

	if (syscall(SYS_finit_module, fd, "", 0) != 0) {
		if (errno == ENOSYS)
			skip_all("kernel has no module support");
		um_result("# finit_module: errno=%d", errno);
		um_res_fail("module_load", "finit_module failed");
		um_res_fail("module_listed", "not loaded");
		um_res_fail("module_unload", "not loaded");
		um_guest_done(1);
	}
	close(fd);
	um_res_ok("module_load");

	if (read_modules(&after))
		skip_all("cannot read /proc/modules");
	for (i = 0; i < after.n; i++) {
		if (!listed(&before, after.names[i])) {
			newmod = after.names[i];
			break;
		}
	}
	if (newmod) {
		um_result("# loaded module: %s", newmod);
		um_res_ok("module_listed");
	} else {
		um_res_fail("module_listed", "no new entry in /proc/modules");
		um_res_fail("module_unload", "module name unknown");
		um_guest_done(1);
	}

	if (syscall(SYS_delete_module, newmod, O_NONBLOCK) != 0) {
		um_result("# delete_module: errno=%d", errno);
		um_res_fail("module_unload", "delete_module failed");
	} else {
		if (read_modules(&after))
			skip_all("cannot read /proc/modules");
		if (!listed(&after, newmod))
			um_res_ok("module_unload");
		else
			um_res_fail("module_unload", "module still in /proc/modules");
	}

	um_guest_done(0);
	return 0;
}
