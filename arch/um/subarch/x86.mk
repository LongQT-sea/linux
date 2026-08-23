# SPDX-License-Identifier: GPL-2.0
# SUBARCH=x86 routes to the x86 backend; defconfig follows the build host.
HEADER_ARCH := x86
ifeq ($(shell uname -m),x86_64)
  KBUILD_DEFCONFIG := x86_64_defconfig
else
  KBUILD_DEFCONFIG := i386_defconfig
endif
