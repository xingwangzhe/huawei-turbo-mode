#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Run a short CPU load while sampling fan RPM and EC temperatures.
#
# The fan itself is EC-controlled on BoF-XX; this script only reads the WMI
# tachometer and temperatures.  It never writes fan registers.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
SECS="${1:-20}"
BIN="${HUAWEI_PERF:-$(command -v huawei-perf || echo "$ROOT/core/build/huawei-perf")}"
BURN="${HUAWEI_BURN:-$(command -v huawei-perf-burn || echo "$ROOT/core/build/burn")}"

if [ "$(id -u)" -ne 0 ]; then
	echo "请用 sudo 运行：sudo $0 [seconds]" >&2
	exit 1
fi
if [ ! -x "$BIN" ] || [ ! -x "$BURN" ]; then
	make -C "$ROOT"
	BIN="$ROOT/core/build/huawei-perf"
	BURN="$ROOT/core/build/burn"
fi

echo "load: ${SECS}s, $(nproc) threads"
"$BURN" "$SECS" "$(nproc)" >/tmp/huawei-fan-check-burn.log 2>&1 &
burn_pid=$!

status=0
"$BIN" fan-health "$SECS" || status=$?
wait "$burn_pid" || true
cat /tmp/huawei-fan-check-burn.log
exit "$status"
