# SUBARCH=s390 routes to the s390x backend; defconfig follows the build
# host ISA (s390x only — a 31-bit backend is out of scope).
HEADER_ARCH := s390
KBUILD_DEFCONFIG := s390_defconfig
