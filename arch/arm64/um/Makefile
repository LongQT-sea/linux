# SPDX-License-Identifier: GPL-2.0
#
# arm64 backend objects for UML. Starts empty by design: the port
# lands the sysdep surfaces in dependency order (setjmp,
# faultinfo/mcontext, ptrace regsets, stub page, signal frame, vdso)
# and each becomes an object here as it is written.

obj-y =

# arm64's native generated headers (cpucap-defs.h & friends), invoked
# from the core archprepare via ARCHPREPARE_BACKEND.
PHONY += kapigen
kapigen:
	$(Q)$(MAKE) $(build)=arch/arm64/tools kapi
