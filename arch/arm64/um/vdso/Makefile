# SPDX-License-Identifier: GPL-2.0
#
# Building vDSO images for arm64 (mirrors the x86 UML vdso Makefile).
#

# files to link into the vdso
vobjs-y := vdso-note.o um_vdso.o sigreturn.o

# files to link into kernel
obj-y += vdso.o vma.o

vobjs := $(foreach F,$(vobjs-y),$(obj)/$F)

$(obj)/vdso.o: $(obj)/vdso.so

targets += vdso.so vdso.so.dbg vdso.lds $(vobjs-y)

CPPFLAGS_vdso.lds += -P -C

VDSO_LDFLAGS_vdso.lds = -Wl,-soname=linux-vdso.so.1 \
       -Wl,-z,max-page-size=4096

$(obj)/vdso.o: $(src)/vdso.S $(obj)/vdso.so

$(obj)/vdso.so.dbg: $(obj)/vdso.lds $(vobjs) FORCE
	$(call if_changed,vdso)

$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# Extract the sigreturn trampoline's offset in the page for the kernel
targets += vdso-offsets.h

quiet_cmd_vdsooff = VDSOOFF $@
      cmd_vdsooff = $(NM) --defined-only $(obj)/vdso.so.dbg | \
		awk '$$3 == "__kernel_rt_sigreturn" { printf "\#define VDSO_SIGRETURN_OFF 0x%s\n", $$1 }' > $@

$(obj)/vdso-offsets.h: $(obj)/vdso.so.dbg
	$(call cmd,vdsooff)

$(obj)/vma.o: $(obj)/vdso-offsets.h
CPPFLAGS_vma.o += -I$(obj)

#
# Don't omit frame pointers for ease of userspace debugging, but do
# optimize sibling calls.
#
CFL := $(PROFILING) -fPIC -O2 -fasynchronous-unwind-tables \
       $(filter -g%,$(KBUILD_CFLAGS)) -fno-stack-protector \
       -fno-omit-frame-pointer -foptimize-sibling-calls

$(vobjs): KBUILD_CFLAGS += $(CFL)

#
# vDSO code runs in userspace and -pg doesn't help with profiling anyway.
#
CFLAGS_REMOVE_vdso-note.o = -pg -fprofile-arcs -ftest-coverage
CFLAGS_REMOVE_um_vdso.o = -pg -fprofile-arcs -ftest-coverage

#
# The DSO images are built using a special linker script.
#
quiet_cmd_vdso = VDSO    $@
      cmd_vdso = $(CC) -nostdlib -o $@ \
		       $(CC_FLAGS_LTO) $(VDSO_LDFLAGS) $(VDSO_LDFLAGS_$(filter %.lds,$(^F))) \
		       -Wl,-T,$(filter %.lds,$^) $(filter %.o,$^)

VDSO_LDFLAGS = -fPIC -shared -Wl,--hash-style=sysv -z noexecstack -Wl,--no-undefined
