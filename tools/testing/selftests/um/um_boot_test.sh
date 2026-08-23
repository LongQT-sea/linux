#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# UML boot-to-marker test.
#
# Boots the UML binary under test once per userspace mode (ptrace and
# seccomp) with a minimal init payload, and passes when the payload's
# completion marker comes back through hostfs with no kernel
# diagnostics in the boot log.
#
# Usage: UML_BIN=/path/to/linux ./um_boot_test.sh
#
# Copyright (C) 2026 Fabian Franz

DIR="$(dirname "$(readlink -f "$0")")"

source "$DIR"/../kselftest/ktap_helpers.sh
source "$DIR"/um_lib.sh

ktap_print_header
um_setup

for mode in $UML_MODES; do
	um_boot boot "$mode" "$DIR/guest_init"

	case "$UM_VERDICT" in
	PASS)
		if um_mode_detected "$mode"; then
			ktap_test_pass "boot $mode"
		else
			ktap_test_fail "boot $mode (booted, but not in $mode mode)"
		fi
		;;
	NOSECCOMP)
		ktap_test_skip "boot $mode (host cannot run seccomp mode)"
		;;
	*)
		ktap_test_fail "boot $mode (verdict=$UM_VERDICT)"
		;;
	esac
done

KSFT_NUM_TESTS=$((KTAP_TESTNO - 1))
ktap_set_plan "$KSFT_NUM_TESTS"
ktap_finished
