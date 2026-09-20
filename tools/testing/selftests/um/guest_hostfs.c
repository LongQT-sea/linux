// SPDX-License-Identifier: GPL-2.0
/*
 * Guest payload for the hostfs root= selftest.
 *
 * Unlike the other payloads it takes its result path from the
 * environment (the kernel passes unconsumed key=value command line
 * words to init as environment): the control boot runs on the host's
 * own / and must never create files there, so a fixed "/result.tap"
 * is not an option.
 *
 *   UM_RESULT	       where to write the result file
 *   UM_EXPECT_PRESENT path that must exist for this boot's root to be
 *		       the intended directory
 *   UM_EXPECT_ABSENT  path that must NOT exist (it exists only in the
 *		       other candidate root, so its presence means the
 *		       wrong directory got mounted)
 *
 * Copyright (C) 2026 Fabian Franz
 */
#include <stdlib.h>
#include <sys/stat.h>

#include "um_guest.h"

int main(void)
{
	const char *res = getenv("UM_RESULT");
	const char *present = getenv("UM_EXPECT_PRESENT");
	const char *absent = getenv("UM_EXPECT_ABSENT");
	struct stat st;

	um_guest_is_init = (getpid() == 1);
	if (um_guest_is_init && res)
		um_result_fd = open(res, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	um_result("PLAN 2");

	if (!present)
		um_res_skip("root_marker", "UM_EXPECT_PRESENT not set");
	else if (stat(present, &st) == 0)
		um_res_ok("root_marker");
	else
		um_res_fail("root_marker", "expected path not visible");

	if (!absent)
		um_res_skip("foreign_marker_absent", "UM_EXPECT_ABSENT not set");
	else if (stat(absent, &st) != 0)
		um_res_ok("foreign_marker_absent");
	else
		um_res_fail("foreign_marker_absent", "foreign path visible");

	um_guest_done(0);
	return 0;
}
