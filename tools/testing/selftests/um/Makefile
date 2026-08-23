# SPDX-License-Identifier: GPL-2.0
# Selftests for User Mode Linux.
#
# The shell tests boot a UML binary (built separately with ARCH=um) and
# take its path from $UML_BIN, so this Makefile only builds the guest
# payloads.  They are static binaries: the guest root is a bare hostfs
# directory with no libraries in it.
#
# For an ARM64 UML binary, cross-build the payloads with e.g.:
#   make CROSS_COMPILE=aarch64-linux-gnu-
# and drive the run with um_arm64_rig.sh (see that script's header).

TEST_PROGS := um_boot_test.sh um_core_test.sh um_module_test.sh \
	      um_bench_test.sh
TEST_FILES := um_lib.sh um_arm64_rig.sh um_rig_driver.sh
TEST_GEN_FILES := guest_init guest_core guest_module guest_bench

CFLAGS += -O2 -Wall -static
LDLIBS += -lpthread

include ../lib.mk

$(TEST_GEN_FILES): $(OUTPUT)/%: %.c um_guest.h
	$(call msg,CC,,$@)
	$(Q)$(CC) $(CFLAGS) $< $(LDLIBS) -o $@
