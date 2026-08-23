# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
#

obj-y := stub.o process.o syscall.o \
	 stub_exe_embed.o
obj-$(CONFIG_MMU) += mmu.o uaccess.o
ifneq ($(CONFIG_MMU),y)
obj-y += nommu.o
endif

# Stub executable

stub_exe_objs-y := stub_exe.o

stub_exe_objs := $(foreach F,$(stub_exe_objs-y),$(obj)/$F)

# Object file containing the ELF executable
$(obj)/stub_exe_embed.o: $(src)/stub_exe_embed.S $(obj)/stub_exe

$(obj)/stub_exe.dbg: $(stub_exe_objs) FORCE
	$(call if_changed,stub_exe)

$(obj)/stub_exe: OBJCOPYFLAGS := -S
$(obj)/stub_exe: $(obj)/stub_exe.dbg FORCE
	$(call if_changed,objcopy)

quiet_cmd_stub_exe = STUB_EXE $@
      cmd_stub_exe = $(CC) -nostdlib -o $@ \
			   $(filter-out $(UM_GPROF_OPT) $(UM_GCOV_OPT),$(KBUILD_CFLAGS)) $(STUB_EXE_LDFLAGS) \
			   $(filter %.o,$^)

STUB_EXE_LDFLAGS = -Wl,-n -static

targets += stub_exe.dbg stub_exe $(stub_exe_objs-y)

# end

# stub.o is in the stub, so it can't be built with profiling
# GCC hardened also auto-enables -fpic, but we need %ebx so it can't work ->
# disable it

CFLAGS_stub.o := $(CFLAGS_NO_HARDENING)
CFLAGS_stub_exe.o := $(CFLAGS_NO_HARDENING)

# stub_signal_restorer() must not touch stack pointer, clang doesn't
# always omit the frame pointer automatically
CFLAGS_stub.o += -fomit-frame-pointer
CFLAGS_stub_exe.o += -fomit-frame-pointer

# The stub must stay call-closed; -mgeneral-regs-only makes GCC
# emit out-of-line memset/memcpy calls for aggregate zeroing, which are
# PC-relative and land nowhere once the stub page is copied to STUB_CODE.
# The host kernel saves/restores FP state around the stub's signal
# handlers (sigframe), so SIMD use in the stub is safe.
# (CFLAGS_REMOVE does not reach UNPROFILE_OBJS: their c_flags are
# rebuilt from USER_CFLAGS in arch/um/scripts/Makefile.rules.)
USER_CFLAGS := $(filter-out -mgeneral-regs-only,$(USER_CFLAGS))

# Clang's stack var initialisation can use memset() or similar and that
# cannot be done for this code since we embed/move the stub
CFLAGS_stub.o += $(call cc-option, -ftrivial-auto-var-init=uninitialized)
CFLAGS_stub_exe.o += $(call cc-option, -ftrivial-auto-var-init=uninitialized)

UNPROFILE_OBJS := stub.o stub_exe.o
KCOV_INSTRUMENT := n

include $(srctree)/arch/um/scripts/Makefile.rules
