#!/bin/busybox sh
# SPDX-License-Identifier: GPL-2.0
#
# In-guest driver for um_arm64_rig.sh.  Runs as PID 1 of the qemu
# guest's initramfs (busybox sh -- keep this strictly POSIX), boots the
# UML binary once per userspace mode with each payload as init, and
# emits one consolidated TAP stream on the serial console between
# UMRIG_TAP_BEGIN / UMRIG_TAP_END markers for the host wrapper to
# extract.
#
# The UML instances use hostfs with the initramfs root as their root
# filesystem, so payload result files land in / and survive the UML
# instance's exit.
#
# Copyright (C) 2026 Fabian Franz

/bin/busybox mkdir -p /proc /sys /dev /tmp /dev/shm
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev
/bin/busybox mount -t tmpfs -o exec,mode=1777 tmpfs /tmp
/bin/busybox mount -t tmpfs -o exec,mode=1777 tmpfs /dev/shm
export TMPDIR=/tmp
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export HOME=/tmp

# Make the host kernel report unhandled user faults (stub deaths).
echo 1 > /proc/sys/debug/exception-trace 2>/dev/null

# Baked by the wrapper: RIG_MODES, RIG_TESTS, RIG_UML_TIMEOUT, RIG_BENCH_ROUNDS
. /rig-config

echo "UMRIG_HOST_UP"
uname -a

TESTNO=1
FAILS=0
TAPBUF=/tap-lines

tap() {
	echo "$1 $TESTNO $2" >> "$TAPBUF"
	TESTNO=$((TESTNO + 1))
}

# run_uml <label> <mode> <payload> <timeout>
run_uml() {
	label="$1"; mode="$2"; payload="$3"; tmo="$4"

	case "$mode" in
	seccomp) secarg="seccomp=on" ;;
	*)	 secarg="seccomp=off" ;;
	esac

	rm -f /result.tap
	echo "=== UML_RUN ${label}/${mode} (timeout ${tmo}s) ==="
	/bin/busybox timeout -s KILL "$tmo" /uml/linux \
		mem=256M panic=-1 rootfstype=hostfs rw "init=$payload" \
		"$secarg" $RIG_UML_EXTRA \
		con=null con0=fd:0,fd:1 ssl=null \
		> "/uml-$label-$mode.log" 2>&1
	rc=$?
	echo "--- uml log ($label/$mode, rc=$rc) ---"
	/bin/busybox cat "/uml-$label-$mode.log"
	echo "--- end uml log ---"

	VERDICT=FAIL
	if /bin/busybox grep -q GUEST_DONE /result.tap 2>/dev/null; then
		if /bin/busybox grep -qE '^(BUG:|WARNING:|Oops|kernel BUG at)' \
			"/uml-$label-$mode.log"; then
			VERDICT=BUG
		else
			VERDICT=PASS
		fi
	elif /bin/busybox grep -q 'SECCOMP userspace requested but not functional' \
		"/uml-$label-$mode.log"; then
		VERDICT=NOSECCOMP
	elif [ "$rc" = "137" ] || [ "$rc" = "124" ]; then
		VERDICT=HANG
	fi
	echo "verdict=$VERDICT"
}

# "name # detail" -> "name (detail)"; plain names pass through
split_detail() {
	case "$1" in
	*" # "*) echo "${1%% # *} (${1#* # })" ;;
	*)	 echo "$1" ;;
	esac
}

# relay_results <prefix> <plan> -- payload result lines to TAP
relay_results() {
	prefix="$1"; plan="$2"
	relayed=0

	if [ -f /result.tap ]; then
		while IFS= read -r line; do
			case "$line" in
			"ok "*)
				tap "ok" "$prefix $(split_detail "${line#ok }")"
				relayed=$((relayed + 1)) ;;
			"not ok "*)
				tap "not ok" "$prefix $(split_detail "${line#not ok }")"
				relayed=$((relayed + 1))
				FAILS=$((FAILS + 1)) ;;
			"skip "*)
				tap "ok" "$prefix $(split_detail "${line#skip }") # SKIP"
				relayed=$((relayed + 1)) ;;
			esac
		done < /result.tap
	fi
	while [ "$relayed" -lt "$plan" ]; do
		tap "not ok" "$prefix unreached # verdict=$VERDICT"
		relayed=$((relayed + 1))
		FAILS=$((FAILS + 1))
	done
}

# one_test <label> <mode> <payload> <plan> <timeout>
one_test() {
	label="$1"; mode="$2"; payload="$3"; plan="$4"; tmo="$5"

	run_uml "$label" "$mode" "$payload" "$tmo"
	case "$VERDICT" in
	PASS)
		relay_results "$mode $label" "$plan"
		tap "ok" "$mode $label run"
		;;
	BUG)
		relay_results "$mode $label" "$plan"
		tap "not ok" "$mode $label run # kernel diagnostics in log"
		FAILS=$((FAILS + 1))
		;;
	NOSECCOMP)
		i=0
		while [ "$i" -lt "$plan" ]; do
			tap "ok" "$mode $label # SKIP host cannot run seccomp mode"
			i=$((i + 1))
		done
		tap "ok" "$mode $label run # SKIP host cannot run seccomp mode"
		;;
	*)
		relay_results "$mode $label" "$plan"
		tap "not ok" "$mode $label run # verdict=$VERDICT"
		FAILS=$((FAILS + 1))
		;;
	esac
}

: > "$TAPBUF"

for mode in $RIG_MODES; do
	for t in $RIG_TESTS; do
		case "$t" in
		boot)	one_test boot "$mode" /guest_init 1 "$RIG_UML_TIMEOUT" ;;
		core)	one_test core "$mode" /guest_core 16 "$RIG_UML_TIMEOUT" ;;
		module)	one_test module "$mode" /guest_module 3 "$RIG_UML_TIMEOUT" ;;
		bench)	one_test bench "$mode" /guest_bench 1 "$RIG_UML_TIMEOUT" ;;
		esac
	done
done

echo "UMRIG_TAP_BEGIN"
echo "TAP version 13"
echo "1..$((TESTNO - 1))"
/bin/busybox cat "$TAPBUF"
echo "# rig totals: tests=$((TESTNO - 1)) fails=$FAILS"
echo "UMRIG_TAP_END"

echo "UMRIG_DONE"
/bin/busybox poweroff -f
