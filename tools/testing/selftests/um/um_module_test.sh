#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# UML module load/unload test.
#
# Needs a module built against the kernel under test.  Point UML_TEST_KO
# at one, or build the kernel with CONFIG_TEST_LKM=m (see this suite's
# `config` fragment) and the default -- lib/test_module.ko next to the
# UML binary's build tree -- is picked up automatically.  Skips cleanly
# when no module is available.
#
# Usage: UML_BIN=/path/to/linux [UML_TEST_KO=/path/to/x.ko] ./um_module_test.sh
#
# Copyright (C) 2026 Fabian Franz

DIR="$(dirname "$(readlink -f "$0")")"

source "$DIR"/../kselftest/ktap_helpers.sh
source "$DIR"/um_lib.sh

NSUBTESTS=3

ktap_print_header
um_setup

if [ -z "${UML_TEST_KO:-}" ]; then
	candidate="$(dirname "$UML_BIN")/lib/test_module.ko"
	[ -f "$candidate" ] && UML_TEST_KO="$candidate"
fi

if [ -z "${UML_TEST_KO:-}" ] || [ ! -f "${UML_TEST_KO}" ]; then
	ktap_skip_all "no test module (set UML_TEST_KO or build with CONFIG_TEST_LKM=m)"
	exit "$KSFT_SKIP"
fi
ktap_print_msg "test module: $UML_TEST_KO"

for mode in $UML_MODES; do
	# The payload expects the module at /test_module.ko regardless of
	# its original name.
	cp "$UML_TEST_KO" "$UM_WORKDIR/test_module.ko"

	um_boot module "$mode" "$DIR/guest_module" "$UM_WORKDIR/test_module.ko"

	case "$UM_VERDICT" in
	PASS|BUG)
		um_relay_results "$mode"
		if [ "$UM_VERDICT" = BUG ]; then
			ktap_test_fail "$mode module run (kernel diagnostics in boot log)"
		else
			ktap_test_pass "$mode module run"
		fi
		;;
	NOSECCOMP)
		um_skip_all_mode "$mode" "$NSUBTESTS" "host cannot run seccomp mode"
		ktap_test_skip "$mode module run (host cannot run seccomp mode)"
		;;
	*)
		um_fail_all "$mode" "$NSUBTESTS" "run verdict=$UM_VERDICT"
		ktap_test_fail "$mode module run (verdict=$UM_VERDICT)"
		;;
	esac
done

KSFT_NUM_TESTS=$((KTAP_TESTNO - 1))
ktap_set_plan "$KSFT_NUM_TESTS"
ktap_finished
