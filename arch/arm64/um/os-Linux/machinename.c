// SPDX-License-Identifier: GPL-2.0
/*
 * arm64 machine-name fixup for UML's utsname: the identity (no x86
 * i686<->x86_64 quirk exists on this backend).
 */
#include <linux/kconfig.h>
#include <string.h>
#include <os.h>

const char *os_machinename_fixup(const char *host_machine)
{
	return host_machine;
}
