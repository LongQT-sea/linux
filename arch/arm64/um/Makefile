# SPDX-License-Identifier: GPL-2.0
#
# arm64 backend objects for UML. Starts with the root parameter file
# (user-offsets -> user_constants.h); the port lands the sysdep
# surfaces in dependency order (setjmp, faultinfo/mcontext, ptrace
# regsets, stub page, signal frame, vdso) and each becomes an object
# here as it is written.

obj-y = delay.o os-Linux/ sys_call_table.o ptrace.o signal.o syscalls.o bugs.o fault.o sysrq.o tls.o

USER_OBJS := ptrace_user.o

# Native arm64 checksum objects (csum_ipv6_magic, do_csum).
subarch-y = ../lib/csum.o

$(obj)/user-offsets.s: c_flags = -Wp,-MD,$(depfile) $(USER_CFLAGS)
targets += user-offsets.s

include/generated/user_constants.h: $(obj)/user-offsets.s FORCE
	$(call filechk,offsets,__USER_CONSTANT_H__)

# arm64's native generated headers (cpucap-defs.h & friends), invoked
# from the core archprepare via ARCHPREPARE_BACKEND.
PHONY += kapigen
kapigen:
	$(Q)$(MAKE) $(build)=arch/arm64/tools kapi
