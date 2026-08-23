// SPDX-License-Identifier: GPL-2.0
/*
 * x86 machine-name quirk for UML's utsname: 32-bit UML on a 64-bit
 * host reports i686, and 64-bit UML on a 32-bit host reports x86_64
 * (historic). Moved verbatim out of arch/um/os-Linux/util.c; on other
 * backends the fixup is the identity.
 */
#include <linux/kconfig.h>
#include <string.h>
#include <os.h>

const char *os_machinename_fixup(const char *host_machine)
{
#if IS_ENABLED(CONFIG_UML_X86)
# if !IS_ENABLED(CONFIG_64BIT)
	if (!strcmp(host_machine, "x86_64"))
		return "i686";
# else
	if (!strcmp(host_machine, "i686"))
		return "x86_64";
# endif
#endif
	return host_machine;
}
