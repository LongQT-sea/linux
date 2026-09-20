#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# hostfs root= test: the mount source names the host directory.
#
# Two boots per userspace mode:
#
#   root=<prepared dir> rootfstype=hostfs must expose the prepared
#   directory as /: init is exec'd from it, a marker file in it is
#   visible at /, the runner's scratch tree is NOT visible, and the
#   result file written to an absolute guest path lands in it.
#
#   the control boot without root= must keep the default, the host's
#   own /: init is exec'd via an absolute path that only resolves in
#   the host filesystem, and the prepared directory's marker is not
#   visible at /.
#
# Usage: UML_BIN=/path/to/linux ./um_hostfs_root_test.sh
#
# Copyright (C) 2026 Fabian Franz

DIR="$(dirname "$(readlink -f "$0")")"

source "$DIR"/../kselftest/ktap_helpers.sh
source "$DIR"/um_lib.sh

ktap_print_header
um_setup

# um_boot_root <name> <mode> <root argument or ""> <init path>
#	       <result file> <extra env...>
#
# um_boot boots on a fresh hostfs= scratch root; this variant instead
# passes the given root= argument (or none) and reads the result file
# from wherever this boot's root makes it land.  Verdict logic and the
# independent kernel-diagnostic scan mirror um_boot.
um_boot_root()
{
	name="$1"
	mode="$2"
	rootarg="$3"
	initarg="$4"
	resfile="$5"
	shift 5

	UM_LOG="$UM_WORKDIR/$name-$mode.log"

	setsid env HOME="$UM_WORKDIR" timeout --signal=KILL \
		--kill-after=10 "$UML_TIMEOUT" \
		${UML_WRAP:-} "$UML_BIN" mem="$UML_MEM" panic=-1 \
		rootfstype=hostfs $rootarg rw "init=$initarg" "$@" \
		con=null con0=fd:0,fd:1 ssl=null \
		$(um_mode_args "$mode") $UML_EXTRA_ARGS \
		</dev/null >"$UM_LOG" 2>&1
	rc=$?

	kbugs=$(grep -acE '^(BUG:|WARNING:|Oops|kernel BUG at|INFO: rcu|INFO: task .* blocked)' \
		"$UM_LOG" 2>/dev/null || true)

	if grep -qF 'GUEST_DONE' "$resfile" 2>/dev/null; then
		if [ "${kbugs:-0}" -gt 0 ]; then
			UM_VERDICT=BUG
		else
			UM_VERDICT=PASS
		fi
	elif grep -q 'SECCOMP userspace requested but not functional' "$UM_LOG" 2>/dev/null; then
		UM_VERDICT=NOSECCOMP
	elif [ "$rc" = "137" ] || [ "$rc" = "124" ]; then
		UM_VERDICT=HANG
	elif grep -qE 'Kernel panic|BUG:|Oops' "$UM_LOG" 2>/dev/null; then
		UM_VERDICT=PANIC
	else
		UM_VERDICT=FAIL
	fi

	ktap_print_msg "$name/$mode: verdict=$UM_VERDICT rc=$rc log=$UM_LOG"
}

# um_root_subtests <prefix> <result dir>
um_root_subtests()
{
	case "$UM_VERDICT" in
	PASS)
		UM_ROOT="$2"
		um_relay_results "$1"
		;;
	NOSECCOMP)
		um_skip_all_mode "$1" 2 "host cannot run seccomp mode"
		;;
	*)
		um_fail_all "$1" 2 "verdict=$UM_VERDICT"
		;;
	esac
}

for mode in $UML_MODES; do
	# The directory root= will name: marker + init + mount points.
	ROOTDIR="$UM_WORKDIR/hostfsroot-$mode"
	mkdir -p "$ROOTDIR/dev" "$ROOTDIR/proc" "$ROOTDIR/tmp"
	cp "$DIR/guest_hostfs" "$ROOTDIR/init"
	chmod 755 "$ROOTDIR/init"
	: > "$ROOTDIR/um_hostfs_root_marker"

	# root= is stored in a 64-byte buffer (saved_root_name in
	# init/do_mounts.c) and silently truncated beyond it, so a deep
	# TMPDIR cannot be tested this way: skip rather than fail on the
	# truncated path's fallback mount.
	if [ "${#ROOTDIR}" -lt 64 ]; then
		um_boot_root root "$mode" "root=$ROOTDIR" /init \
			"$ROOTDIR/result.tap" \
			"UM_RESULT=/result.tap" \
			"UM_EXPECT_PRESENT=/um_hostfs_root_marker" \
			"UM_EXPECT_ABSENT=$UM_WORKDIR"
		um_root_subtests "root= $mode" "$ROOTDIR"
	else
		um_skip_all_mode "root= $mode" 2 \
			"path longer than the kernel's 64-byte root= buffer"
	fi

	# Control: no root=.  The default must remain the host's /:
	# init and the result file are reached via absolute host paths,
	# and the prepared directory's marker must not be visible at /.
	CTLDIR="$UM_WORKDIR/hostfsctl-$mode"
	mkdir -p "$CTLDIR"
	cp "$DIR/guest_hostfs" "$CTLDIR/init"
	chmod 755 "$CTLDIR/init"

	um_boot_root control "$mode" "" "$CTLDIR/init" \
		"$CTLDIR/result.tap" \
		"UM_RESULT=$CTLDIR/result.tap" \
		"UM_EXPECT_PRESENT=$CTLDIR/init" \
		"UM_EXPECT_ABSENT=/um_hostfs_root_marker"
	um_root_subtests "control $mode" "$CTLDIR"
done

KSFT_NUM_TESTS=$((KTAP_TESTNO - 1))
ktap_set_plan "$KSFT_NUM_TESTS"
ktap_finished
