#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# UML syscall/fault microbenchmark driver.  See guest_bench.c for what
# the arms measure.
#
# Measurement discipline (after the um-arm64 harness by Oleksii
# Zakharov): every condition -- each userspace mode, plus a native run
# of the same binary when the payload matches the host architecture --
# is measured in the SAME session, interleaved boot by boot, so a
# difference between two conditions cannot be explained by drift
# between two runs.  Reported numbers are medians across all rounds of
# all boots, printed with their min..max spread; the `compute` arm is a
# validator that flags the whole run when it moves, because pure
# arithmetic must not care which kernel services the syscalls.
#
# This is a benchmark, not a regression gate: TAP results only say
# whether each condition RAN; the numbers are diagnostics for humans.
#
# On machines with heterogeneous cores (or noisy neighbours), pin the
# measured processes:  UML_WRAP="taskset -c 0-11" ./um_bench_test.sh
# The validator below tells you when you needed to.
#
# Usage: UML_BIN=/path/to/linux [UML_BENCH_BOOTS=2] ./um_bench_test.sh
#
# Copyright (C) 2026 Fabian Franz

DIR="$(dirname "$(readlink -f "$0")")"

source "$DIR"/../kselftest/ktap_helpers.sh
source "$DIR"/um_lib.sh

UML_BENCH_BOOTS="${UML_BENCH_BOOTS:-2}"
# Benchmark boots run longer than functional boots.
UML_TIMEOUT="${UML_BENCH_TIMEOUT:-300}"

ktap_print_header
um_setup

ROWS="$UM_WORKDIR/rows"
: > "$ROWS"

# Can the payload run natively?  Only when it was built for the host
# architecture; --true is a no-op entry point that proves it.
native_ok=0
if "$DIR/guest_bench" --true 2>/dev/null; then
	native_ok=1
	ktap_print_msg "native baseline: enabled (payload runs on this host)"
else
	ktap_print_msg "native baseline: skipped (payload does not run on this host)"
fi

declare -A mode_ok

b=1
while [ "$b" -le "$UML_BENCH_BOOTS" ]; do
	for mode in $UML_MODES; do
		um_boot bench "$mode" "$DIR/guest_bench"
		case "$UM_VERDICT" in
		PASS|BUG)
			grep -a '^BENCH' "$UM_ROOT/result.tap" | \
				sed "s/^/$mode /" >> "$ROWS"
			[ -z "${mode_ok[$mode]:-}" ] && mode_ok[$mode]=$UM_VERDICT
			;;
		NOSECCOMP)
			mode_ok[$mode]=NOSECCOMP
			;;
		*)
			mode_ok[$mode]=$UM_VERDICT
			;;
		esac
	done
	if [ "$native_ok" = 1 ]; then
		${UML_WRAP:-} "$DIR/guest_bench" 2>/dev/null | \
			grep -a '^BENCH' | \
			sed 's/^/native /' >> "$ROWS" || native_ok=0
	fi
	b=$((b + 1))
done

# Aggregate: median (and spread) per condition and arm over every
# recorded round of every boot.
awk '
$2 == "BENCHROW" {
	cond = $1
	for (i = 3; i <= NF; i++) {
		if ($i ~ /^arm=/)  { a = substr($i, 5) }
		if ($i ~ /^us=/)   { v = substr($i, 4) + 0 }
	}
	if (v >= 0) {
		key = cond "/" a
		n[key]++
		val[key, n[key]] = v
		if (!(a in arms)) { arms[a] = ++narm; armname[narm] = a }
		conds[cond] = 1
	}
}
END {
	for (k in n) {
		m = n[k]
		# insertion sort; m is small
		for (i = 2; i <= m; i++) {
			x = val[k, i]
			for (j = i - 1; j >= 1 && val[k, j] > x; j--)
				val[k, j + 1] = val[k, j]
			val[k, j + 1] = x
		}
		med = (m % 2) ? val[k, int(m / 2) + 1] \
			      : (val[k, m / 2] + val[k, m / 2 + 1]) / 2
		printf "# %-24s median=%10.3f us/op  min=%10.3f max=%10.3f n=%d\n",
			k, med, val[k, 1], val[k, m], m
		medv[k] = med
	}
	# Validator: compute must agree across conditions.
	lo = -1; hi = -1
	for (c in conds) {
		k = c "/compute"
		if (k in medv) {
			if (lo < 0 || medv[k] < lo) lo = medv[k]
			if (hi < 0 || medv[k] > hi) hi = medv[k]
		}
	}
	if (lo > 0 && (hi - lo) / lo > 0.10)
		printf "# BENCH_INVALID: compute medians spread %.1f%% across conditions -- machine moved, numbers suspect\n",
			(hi - lo) / lo * 100
	else if (lo > 0)
		printf "# validator: compute stable across conditions (spread %.1f%%)\n",
			(hi - lo) / lo * 100
}
' "$ROWS" | sort

for mode in $UML_MODES; do
	case "${mode_ok[$mode]:-MISSING}" in
	PASS)
		ktap_test_pass "bench $mode"
		;;
	BUG)
		ktap_test_fail "bench $mode (kernel diagnostics during benchmark)"
		;;
	NOSECCOMP)
		ktap_test_skip "bench $mode (host cannot run seccomp mode)"
		;;
	*)
		ktap_test_fail "bench $mode (verdict=${mode_ok[$mode]:-none})"
		;;
	esac
done

KSFT_NUM_TESTS=$((KTAP_TESTNO - 1))
ktap_set_plan "$KSFT_NUM_TESTS"
ktap_finished
