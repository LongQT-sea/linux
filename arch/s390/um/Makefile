# SPDX-License-Identifier: GPL-2.0
#
# s390x backend objects for UML. Starts with the root parameter file
# (user-offsets -> user_constants.h); the port lands the sysdep
# surfaces in dependency order and each becomes an object here as it
# is written (M3b bring-up, per DESIGN-um-s390x §2).

obj-y = ptrace.o delay.o os-Linux/ sys_call_table.o ptrace_user.o syscalls.o bugs.o fault.o sysrq.o signal.o tls.o vdso/ setjmp_64.o stub_segv.o cpuinfo.o csum.o

# No subarch composite: the backend reuses no native objects. The
# CKSM-based native csum-partial.o needs kernel_fpu/thread_struct
# machinery that has no UML meaning; csum.c above provides the two
# out-of-line entry points instead. Static helpers stay shared via
# <asm/checksum.h>.

$(obj)/user-offsets.s: c_flags = -Wp,-MD,$(depfile) $(USER_CFLAGS)
targets += user-offsets.s

include/generated/user_constants.h: $(obj)/user-offsets.s FORCE
	$(call filechk,offsets,__USER_CONSTANT_H__)

UNPROFILE_OBJS := stub_segv.o
CFLAGS_stub_segv.o := $(CFLAGS_NO_HARDENING)

# USER_OBJS (%_user.o) get the host-side compile flags via these rules.
include $(srctree)/arch/um/scripts/Makefile.rules
