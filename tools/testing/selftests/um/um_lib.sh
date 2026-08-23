# SPDX-License-Identifier: GPL-2.0
#
# Shared plumbing for the UML selftests: boot a UML binary with a test
# payload as init and decide, mechanically, whether the run worked.
#
# The run/verdict discipline here follows the um-arm64 bring-up harness
# by Oleksii Zakharov <contact@zalexdev.com>:
#
#   * time-bounded -- a failing port hangs far more often than it
#     crashes, so every run is under `timeout`;
#   * marker, not exit code -- success is a marker string the payload
#     wrote, never an exit status: UML can exit 0 having run nothing,
#     and with panic=-1 several legitimate end states are fatal signals;
#   * panic=-1 -- so a guest panic terminates the process instead of
#     sitting at a dead prompt until the timeout;
#   * the log is scanned for kernel diagnostics INDEPENDENTLY of the
#     marker -- a run that reaches its marker while the kernel reports
#     "BUG:" has not passed.
#
# Environment:
#   UML_BIN	 path to the UML `linux` binary (required; the callers
#		 skip-all when it is missing)
#   UML_MODES	 which userspace modes to test ("ptrace seccomp" default)
#   UML_MEM	 guest memory size (default 256M)
#   UML_TIMEOUT	 per-boot timeout in seconds (default 120)
#   UML_EXTRA_ARGS  extra kernel command line arguments
#   UML_WRAP	 command prefix for the UML process, e.g.
#		 "taskset -c 0-11" to pin benchmark runs on a machine
#		 with heterogeneous cores
#
# After um_boot returns, the caller reads:
#   UM_VERDICT	 PASS | BUG | HANG | PANIC | NOSECCOMP | FAIL
#   UM_ROOT	 the scratch root (payload results in $UM_ROOT/result.tap)
#   UM_LOG	 the console log
#
# Copyright (C) 2026 Fabian Franz

UML_MODES="${UML_MODES:-ptrace seccomp}"
UML_MEM="${UML_MEM:-256M}"
UML_TIMEOUT="${UML_TIMEOUT:-120}"
UML_EXTRA_ARGS="${UML_EXTRA_ARGS:-}"

UM_WORKDIR=""

um_cleanup()
{
	[ -n "$UM_WORKDIR" ] && rm -rf "$UM_WORKDIR"
}

# um_setup <ktap-helpers-loaded caller> -- validate UML_BIN, make workdir
um_setup()
{
	if [ -z "${UML_BIN:-}" ]; then
		ktap_skip_all "UML_BIN is not set (point it at a built UML 'linux' binary)"
		exit "$KSFT_SKIP"
	fi
	if [ ! -x "$UML_BIN" ]; then
		ktap_skip_all "UML_BIN '$UML_BIN' is not an executable"
		exit "$KSFT_SKIP"
	fi
	UM_WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/um-selftest.XXXXXX")"
	trap um_cleanup EXIT
	ktap_print_msg "UML_BIN=$UML_BIN"
	ktap_print_msg "modes: $UML_MODES"
}

# um_mode_args <mode> -> kernel argument selecting the mode
um_mode_args()
{
	case "$1" in
	ptrace)  echo "seccomp=off" ;;
	seccomp) echo "seccomp=on" ;;
	*)	 echo "" ;;
	esac
}

# um_boot <name> <mode> <payload> [extra file to copy into the root...]
#
# Boots $UML_BIN with $payload as init on a fresh hostfs root and sets
# UM_VERDICT / UM_ROOT / UM_LOG.
um_boot()
{
	name="$1"
	mode="$2"
	payload="$3"
	shift 3

	UM_ROOT="$UM_WORKDIR/$name-$mode"
	UM_LOG="$UM_ROOT.log"
	rm -rf "$UM_ROOT"
	# `dev` is a mount point for devtmpfs, `proc` for the payload's
	# proc mount, `tmp` for whatever the payload spawns.
	mkdir -p "$UM_ROOT/dev" "$UM_ROOT/proc" "$UM_ROOT/tmp"
	cp "$payload" "$UM_ROOT/init"
	chmod 755 "$UM_ROOT/init"
	for f in "$@"; do
		cp "$f" "$UM_ROOT/"
	done

	# setsid + a process-group kill: stub children must not outlive
	# the timeout.  HOME is redirected so ~/.uml state stays in the
	# scratch directory.
	setsid env HOME="$UM_ROOT" timeout --signal=KILL \
		--kill-after=10 "$UML_TIMEOUT" \
		${UML_WRAP:-} "$UML_BIN" mem="$UML_MEM" panic=-1 \
		rootfstype=hostfs hostfs="$UM_ROOT" rw init=/init \
		con=null con0=fd:0,fd:1 ssl=null \
		$(um_mode_args "$mode") $UML_EXTRA_ARGS \
		</dev/null >"$UM_LOG" 2>&1
	rc=$?

	# Kernel diagnostics, scanned independently of the marker.  Not
	# matching "Kernel panic" here: runs end by design in a panic
	# when init exits unexpectedly, and that is the FAIL/PANIC path
	# below, not a BUG.
	kbugs=$(grep -acE '^(BUG:|WARNING:|Oops|kernel BUG at|INFO: rcu|INFO: task .* blocked)' \
		"$UM_LOG" 2>/dev/null || true)

	if grep -qF 'GUEST_DONE' "$UM_ROOT/result.tap" 2>/dev/null; then
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
	if [ "${kbugs:-0}" -gt 0 ]; then
		grep -aE '^(BUG:|WARNING:|Oops|kernel BUG at|INFO: rcu|INFO: task .* blocked)' \
			"$UM_LOG" | sort | uniq -c | \
			while read -r line; do
				ktap_print_msg "kernel diagnostic: $line"
			done
	fi
}

# um_mode_detected <mode> -- sanity: did the boot log show the mode we
# asked for?  (The seccomp probe line only appears when seccomp was
# requested; the ptrace checks only run in ptrace mode.)
um_mode_detected()
{
	case "$1" in
	seccomp)
		grep -q 'Checking that seccomp filters can be installed...OK' \
			"$UM_LOG" 2>/dev/null
		;;
	ptrace)
		grep -q 'Checking that ptrace can change system call numbers...OK' \
			"$UM_LOG" 2>/dev/null
		;;
	esac
}

# um_relay_results <prefix> -- turn the payload's result lines into KTAP
# results.  The payload writes "ok NAME", "not ok NAME # detail" or
# "skip NAME # reason"; a missing or truncated result file after a PASS
# verdict cannot happen (GUEST_DONE is its last line).
um_relay_results()
{
	prefix="$1"

	# Payload detail arrives as "name # detail"; fold the detail into
	# the description so it cannot be mistaken for a TAP directive.
	while IFS= read -r line; do
		case "$line" in
		"ok "*)
			ktap_test_pass "$prefix $(um_split_detail "${line#ok }")"
			;;
		"not ok "*)
			ktap_test_fail "$prefix $(um_split_detail "${line#not ok }")"
			;;
		"skip "*)
			ktap_test_skip "$prefix $(um_split_detail "${line#skip }")"
			;;
		esac
	done < "$UM_ROOT/result.tap"
}

# "name # detail" -> "name (detail)"; plain names pass through
um_split_detail()
{
	case "$1" in
	*" # "*)
		echo "${1%% # *} (${1#* # })"
		;;
	*)
		echo "$1"
		;;
	esac
}

# um_plan_of_payload -- the PLAN n the payload declared, or 0
um_payload_plan()
{
	sed -n 's/^PLAN \([0-9]*\)$/\1/p' "$UM_ROOT/result.tap" 2>/dev/null | head -1
}

# um_fail_all <prefix> <n> <why> -- emit n failures for a boot that
# never produced results
um_fail_all()
{
	prefix="$1"
	n="$2"
	why="$3"
	i=1

	while [ "$i" -le "$n" ]; do
		ktap_test_fail "$prefix subtest $i ($why)"
		i=$((i + 1))
	done
}

# um_skip_all_mode <prefix> <n> <why>
um_skip_all_mode()
{
	prefix="$1"
	n="$2"
	why="$3"
	i=1

	while [ "$i" -le "$n" ]; do
		ktap_test_skip "$prefix subtest $i ($why)"
		i=$((i + 1))
	done
}
