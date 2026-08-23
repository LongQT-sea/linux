// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal init payload for the UML boot test.
 *
 * Success is a marker string in the result file, never an exit code:
 * a UML instance can exit 0 having run nothing at all, and with panic=-1
 * several legitimate end states are fatal signals.  So this payload only
 * has to prove that the kernel booted far enough to exec init, that a
 * write through hostfs reaches the host, and that power-off works.
 *
 * Copyright (C) 2026 Fabian Franz
 */
#include "um_guest.h"

int main(void)
{
	um_guest_setup();
	um_result("PLAN 1");
	um_res_ok("boot_to_init");
	um_guest_done(0);
	return 0;
}
