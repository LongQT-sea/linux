#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Optional wrapper: run the UML selftests against an ARM64 UML binary
# on a non-arm64 host, using qemu-system-aarch64 as the host kernel.
#
# Why a system emulator and not qemu-user: UML's process model IS the
# host's ptrace/seccomp machinery, and qemu-user emulates neither
# (ptrace of guest children returns ENOSYS, and seccomp filters would
# be evaluated by the real host kernel against qemu's own syscalls).
# So an arm64 UML needs a real arm64 kernel under it; TCG provides one
# anywhere.
#
# NOT part of the default kselftest run.  Requirements, all supplied by
# the caller:
#
#   UML_BIN	 the ARM64 UML `linux` binary under test (required)
#   RIG_KERNEL	 an arm64 guest kernel Image (arm64 defconfig works;
#		 needs SECCOMP_FILTER for seccomp mode) (required)
#   RIG_BUSYBOX	 a static arm64 busybox binary (required)
#   RIG_LIBDIR	 directory with ld-linux-aarch64.so.1 and libc.so.6 for
#		 a dynamically linked UML binary (default:
#		 /usr/aarch64-linux-gnu/lib; ignored if UML_BIN is
#		 static)
#   RIG_PAYLOADS directory with the guest_* payloads cross-built for
#		 arm64 ("make CROSS_COMPILE=aarch64-linux-gnu-" in this
#		 directory) (default: this script's directory)
#   RIG_KO	 optional .ko built from the same tree/config as UML_BIN
#		 for the module test
#   RIG_MODES	 default "seccomp ptrace"
#   RIG_TESTS	 default "boot core module"; add "bench" if you must,
#		 but timing under TCG measures qemu, not UML
#   RIG_OUT	 artifact directory (default: mktemp)
#   RIG_CPU/RIG_SMP/RIG_MEM/RIG_TIMEOUT
#		 qemu knobs (default: max/4/2048/2400).  Use
#		 "RIG_CPU=max,sve=off,sme=off" for an M2-like host
#		 without SVE.
#
# The guest driver (um_rig_driver.sh) emits one TAP stream between
# markers on the serial console; this wrapper extracts and reprints it,
# and exits non-zero if the stream is missing or contains failures.
#
# Copyright (C) 2026 Fabian Franz

set -uo pipefail

DIR="$(dirname "$(readlink -f "$0")")"

RIG_LIBDIR="${RIG_LIBDIR:-/usr/aarch64-linux-gnu/lib}"
RIG_PAYLOADS="${RIG_PAYLOADS:-$DIR}"
RIG_MODES="${RIG_MODES:-seccomp ptrace}"
RIG_TESTS="${RIG_TESTS:-boot core module}"
RIG_CPU="${RIG_CPU:-max}"
RIG_SMP="${RIG_SMP:-4}"
RIG_MEM="${RIG_MEM:-2048}"
RIG_TIMEOUT="${RIG_TIMEOUT:-2400}"
RIG_UML_TIMEOUT="${RIG_UML_TIMEOUT:-600}"
RIG_QEMU="${RIG_QEMU:-qemu-system-aarch64}"

die() { echo "um_arm64_rig: $*" >&2; exit 1; }

[ -n "${UML_BIN:-}" ] || die "set UML_BIN to the arm64 UML binary"
[ -f "$UML_BIN" ] || die "UML_BIN '$UML_BIN' not found"
[ -n "${RIG_KERNEL:-}" ] || die "set RIG_KERNEL to an arm64 kernel Image"
[ -f "$RIG_KERNEL" ] || die "RIG_KERNEL '$RIG_KERNEL' not found"
[ -n "${RIG_BUSYBOX:-}" ] || die "set RIG_BUSYBOX to a static arm64 busybox"
[ -f "$RIG_BUSYBOX" ] || die "RIG_BUSYBOX '$RIG_BUSYBOX' not found"
command -v "$RIG_QEMU" >/dev/null || die "$RIG_QEMU not installed"
command -v cpio >/dev/null || die "cpio not installed"

for p in guest_init guest_core guest_module guest_bench; do
	[ -f "$RIG_PAYLOADS/$p" ] || \
		die "payload $RIG_PAYLOADS/$p missing (make CROSS_COMPILE=aarch64-linux-gnu-)"
done

RIG_OUT="${RIG_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/um-arm64-rig.XXXXXX")}"
ROOT="$RIG_OUT/initramfs-root"
LOG="$RIG_OUT/serial.log"

rm -rf "$ROOT"
mkdir -p "$ROOT/bin" "$ROOT/sbin" "$ROOT/proc" "$ROOT/sys" "$ROOT/dev" \
	 "$ROOT/tmp" "$ROOT/uml" "$ROOT/lib"

cp "$RIG_BUSYBOX" "$ROOT/bin/busybox"
chmod 755 "$ROOT/bin/busybox"
for app in sh mount umount echo cat grep timeout poweroff uname sleep; do
	ln -sf busybox "$ROOT/bin/$app"
done
ln -sf ../bin/busybox "$ROOT/sbin/poweroff"

# A dynamically linked UML binary needs the loader and libc inside the
# guest; a static one does not, and the copies are then just unused.
if [ -f "$RIG_LIBDIR/ld-linux-aarch64.so.1" ]; then
	mkdir -p "$ROOT/lib/aarch64-linux-gnu"
	cp "$RIG_LIBDIR/ld-linux-aarch64.so.1" "$ROOT/lib/"
	# libgcc_s: glibc's pthread machinery dlopens it at exit.
	for l in libc.so.6 libgcc_s.so.1; do
		[ -f "$RIG_LIBDIR/$l" ] || continue
		cp "$RIG_LIBDIR/$l" "$ROOT/lib/"
		cp "$RIG_LIBDIR/$l" "$ROOT/lib/aarch64-linux-gnu/"
	done
fi

cp "$UML_BIN" "$ROOT/uml/linux"
chmod 755 "$ROOT/uml/linux"
for p in guest_init guest_core guest_module guest_bench; do
	cp "$RIG_PAYLOADS/$p" "$ROOT/$p"
	chmod 755 "$ROOT/$p"
done
if [ -n "${RIG_KO:-}" ]; then
	cp "$RIG_KO" "$ROOT/test_module.ko"
fi

cp "$DIR/um_rig_driver.sh" "$ROOT/init"
chmod 755 "$ROOT/init"
{
	echo "RIG_MODES='$RIG_MODES'"
	echo "RIG_TESTS='$RIG_TESTS'"
	echo "RIG_UML_TIMEOUT='$RIG_UML_TIMEOUT'"
	echo "RIG_UML_EXTRA='${RIG_UML_EXTRA:-}'"
} > "$ROOT/rig-config"

(cd "$ROOT" && find . | LC_ALL=C sort | \
	cpio -o -H newc --owner=0:0 2>/dev/null | gzip -1) \
	> "$RIG_OUT/initramfs.cpio.gz"

{
	echo "=== um_arm64_rig attestation ==="
	echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "UML binary sha256: $(sha256sum "$UML_BIN" | cut -d' ' -f1)"
	echo "guest kernel sha256: $(sha256sum "$RIG_KERNEL" | cut -d' ' -f1)"
	echo "initramfs sha256: $(sha256sum "$RIG_OUT/initramfs.cpio.gz" | cut -d' ' -f1)"
	echo "qemu: $($RIG_QEMU --version | head -1)"
	echo "cpu=$RIG_CPU smp=$RIG_SMP mem=$RIG_MEM"
	echo "modes=$RIG_MODES tests=$RIG_TESTS"
	echo "=== end attestation ==="
} | tee "$LOG"

timeout --signal=KILL "$RIG_TIMEOUT" "$RIG_QEMU" \
	-M virt -cpu "$RIG_CPU" -m "$RIG_MEM" -smp "$RIG_SMP" \
	-nographic -no-reboot \
	-kernel "$RIG_KERNEL" \
	-initrd "$RIG_OUT/initramfs.cpio.gz" \
	-append "console=ttyAMA0 panic=-1" \
	>> "$LOG" 2>&1
qrc=$?
echo "QEMU_EXIT=$qrc" >> "$LOG"

# Extract the TAP stream the driver emitted.
if ! grep -q '^UMRIG_TAP_BEGIN' "$LOG"; then
	echo "TAP version 13"
	echo "1..1"
	echo "not ok 1 arm64 rig produced no TAP stream (see $LOG)"
	exit 1
fi
sed -n '/^UMRIG_TAP_BEGIN/,/^UMRIG_TAP_END/p' "$LOG" | \
	grep -v '^UMRIG_TAP_' | tr -d '\r'

echo "# full serial log: $LOG"
if sed -n '/^UMRIG_TAP_BEGIN/,/^UMRIG_TAP_END/p' "$LOG" | \
	grep -q '^not ok'; then
	exit 1
fi
exit 0
