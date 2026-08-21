# SPDX-License-Identifier: GPL-2.0
#
# s390x backend objects for UML. Starts with the root parameter file
# (user-offsets -> user_constants.h); the port lands the sysdep
# surfaces in dependency order and each becomes an object here as it
# is written (M3b bring-up, per DESIGN-um-s390x §2).

obj-y =

$(obj)/user-offsets.s: c_flags = -Wp,-MD,$(depfile) $(USER_CFLAGS)
targets += user-offsets.s

include/generated/user_constants.h: $(obj)/user-offsets.s FORCE
	$(call filechk,offsets,__USER_CONSTANT_H__)

# USER_OBJS (%_user.o) get the host-side compile flags via these rules.
include $(srctree)/arch/um/scripts/Makefile.rules
