# SPDX-License-Identifier: GPL-2.0
#
# arm64 backend objects for UML. Starts with the root parameter file
# (user-offsets -> user_constants.h); the port lands the sysdep
# surfaces in dependency order (setjmp, faultinfo/mcontext, ptrace
# regsets, stub page, signal frame, vdso) and each becomes an object
# here as it is written.

obj-y = alternatives.o ptrace.o subarch.o delay.o os-Linux/ sys_call_table.o ptrace_user.o syscalls.o bugs.o fault.o sysrq.o signal.o tls.o vdso/ setjmp_64.o stub_segv.o cpuinfo.o

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

UNPROFILE_OBJS := stub_segv.o
CFLAGS_stub_segv.o := $(CFLAGS_NO_HARDENING)

# USER_OBJS (%_user.o) get the host-side compile flags via these rules.
include $(srctree)/arch/um/scripts/Makefile.rules
