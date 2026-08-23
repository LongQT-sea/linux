#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# UML in-guest core battery: fork/exec, memory faults, TLS across fork,
# syscall restart under signals, and signal-frame round trips.  See
# guest_core.c for what each subtest covers and why.
#
# Boots the guest_core payload once per userspace mode and relays its
# per-subtest results.  A boot that dies mid-battery reports the
# subtests it never reached as failures rather than forgetting them.
#
# Usage: UML_BIN=/path/to/linux ./um_core_test.sh
#
# Copyright (C) 2026 Fabian Franz

DIR="$(dirname "$(readlink -f "$0")")"

source "$DIR"/../kselftest/ktap_helpers.sh
source "$DIR"/um_lib.sh

# Keep in step with guest_core.c (it also declares PLAN in its output;
# this is the fallback when a boot produces no output at all).
NSUBTESTS=16

ktap_print_header
um_setup

for mode in $UML_MODES; do
	um_boot core "$mode" "$DIR/guest_core"

	case "$UM_VERDICT" in
	PASS|BUG)
		plan="$(um_payload_plan)"
		[ -n "$plan" ] || plan="$NSUBTESTS"
		before=$KTAP_TESTNO
		um_relay_results "$mode"
		relayed=$((KTAP_TESTNO - before))
		if [ "$relayed" -lt "$plan" ]; then
			um_fail_all "$mode" $((plan - relayed)) \
				"battery died after $relayed of $plan subtests"
		fi
		if [ "$UM_VERDICT" = BUG ]; then
			ktap_test_fail "$mode battery (kernel diagnostics in boot log)"
		else
			ktap_test_pass "$mode battery"
		fi
		;;
	NOSECCOMP)
		um_skip_all_mode "$mode" "$NSUBTESTS" "host cannot run seccomp mode"
		ktap_test_skip "$mode battery (host cannot run seccomp mode)"
		;;
	*)
		# Salvage whatever the payload managed to report before
		# the run died, then fail the remainder.
		plan="$(um_payload_plan)"
		[ -n "$plan" ] || plan="$NSUBTESTS"
		before=$KTAP_TESTNO
		if [ -f "$UM_ROOT/result.tap" ]; then
			um_relay_results "$mode"
		fi
		relayed=$((KTAP_TESTNO - before))
		if [ "$relayed" -lt "$plan" ]; then
			um_fail_all "$mode" $((plan - relayed)) \
				"run verdict=$UM_VERDICT"
		fi
		ktap_test_fail "$mode battery (verdict=$UM_VERDICT)"
		;;
	esac
done

KSFT_NUM_TESTS=$((KTAP_TESTNO - 1))
ktap_set_plan "$KSFT_NUM_TESTS"
ktap_finished
