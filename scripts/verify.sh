#!/usr/bin/env bash
# Measure the real effect of the Huawei power-unlock flag.
#
# Run as root.  The script:
#   1. records balanced-mode limits and load behaviour,
#   2. sends the vendor power-unlock command through huawei-wmi debugfs,
#   3. repeats the same measurements,
#   4. restores the original state and writes a summary.
#
# It deliberately keeps all vendor/thermal protection in place.  The EC still
# owns fan control and emergency throttling.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/core/build/huawei-perf"
BURN="$ROOT/core/build/burn"
OUT="$ROOT/results/$(date +%Y%m%d-%H%M%S)"
ALLOW_BATTERY=0
BURN_SECONDS=30
THREADS="$(nproc)"

usage() {
	cat <<EOF
usage: sudo $0 [--allow-battery] [--seconds N] [--threads N]
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
	--allow-battery) ALLOW_BATTERY=1 ;;
	--seconds) BURN_SECONDS="$2"; shift ;;
	--threads) THREADS="$2"; shift ;;
	-h|--help) usage; exit 0 ;;
	*) echo "unknown argument: $1" >&2; usage; exit 2 ;;
	esac
	shift
done

if [ "$(id -u)" -ne 0 ]; then
	echo "错误：请用 sudo 运行（需要 debugfs、RAPL 和 MSR 访问）。" >&2
	echo "  sudo $0" >&2
	exit 1
fi

if [ ! -x "$BIN" ] || [ ! -x "$BURN" ]; then
	make -C "$ROOT"
fi

if [ ! -e /sys/kernel/debug/huawei-wmi/call ]; then
	echo "错误：/sys/kernel/debug/huawei-wmi/call 不存在。" >&2
	echo "检查：modprobe huawei_wmi；mount -t debugfs debugfs /sys/kernel/debug" >&2
	exit 1
fi

for p in /sys/class/power_supply/A*/online; do
	[ -e "$p" ] || continue
	if [ "$(cat "$p")" != "1" ] && [ "$ALLOW_BATTERY" -ne 1 ]; then
		echo "错误：当前未接电源（$p=$(cat "$p")）。性能模式通常只在插电时生效。" >&2
		echo "接上电源后重试，或加 --allow-battery 强制测试。" >&2
		exit 1
	fi
done

for t in turbostat 7z awk; do
	command -v "$t" >/dev/null || { echo "缺少工具：$t" >&2; exit 1; }
done

mkdir -p "$OUT"

log() { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }

snapshot() {
	local tag="$1"
	"$BIN" status >"$OUT/status-$tag.txt" 2>&1 || true
	cat "$OUT/status-$tag.txt"
}

parse_turbostat() {
	# Prints: avg_watts max_temp avg_mhz avg_bzy samples
	# Skips the first two samples so start-up and the pre-load second do not
	# dilute the steady-state average.
	awk '
		/PkgWatt/ {
			for (i = 1; i <= NF; i++) {
				if ($i == "PkgWatt") w = i
				if ($i == "PkgTmp") t = i
				if ($i == "Avg_MHz") f = i
				if ($i == "Bzy_MHz") b = i
			}
			next
		}
		w && $w ~ /^[0-9.]+$/ {
			n++
			if (n <= 2) next
			W += $w
			F += $f
			B += $b
			if ($t + 0 > T) T = $t
			S++
		}
		END {
			if (S)
				printf "%.2f %.1f %.0f %.0f %d\n",
				       W / S, T, F / S, B / S, S
			else
				print "nan nan nan nan 0"
		}
	' "$1"
}

idle_test() {
	local tag="$1"
	log "idle / $tag"
	turbostat --quiet --show PkgWatt,PkgTmp,Avg_MHz,Bzy_MHz \
		--interval 1 --num_iterations 6 \
		>"$OUT/idle-$tag.turbostat" 2>&1 || true
	parse_turbostat "$OUT/idle-$tag.turbostat" >"$OUT/idle-$tag.txt"
	cat "$OUT/idle-$tag.txt"
}

load_test() {
	local tag="$1"

	log "7z benchmark / $tag"
	7z b -mmt16 -bd >"$OUT/7z-$tag.txt" 2>&1 || true
	grep -E '^Tot:' "$OUT/7z-$tag.txt" | tail -1 || echo "Tot: unavailable"

	log "sustained burn (${BURN_SECONDS}s, ${THREADS} threads) / $tag"
	turbostat --quiet --show PkgWatt,PkgTmp,Avg_MHz,Bzy_MHz,CPU%c0 \
		--interval 1 --num_iterations $((BURN_SECONDS + 3)) \
		>"$OUT/load-$tag.turbostat" 2>&1 &
	local tpid=$!
	"$BURN" "$BURN_SECONDS" "$THREADS" >"$OUT/burn-$tag.txt" 2>&1 || true
	wait "$tpid" || true
	parse_turbostat "$OUT/load-$tag.turbostat" >"$OUT/load-$tag.txt"
	cat "$OUT/burn-$tag.txt"
	cat "$OUT/load-$tag.txt"
}

log "preflight"
{
	echo "date: $(date -Is)"
	echo "kernel: $(uname -a)"
	echo "cmdline: $(cat /proc/cmdline)"
	echo "product: $(cat /sys/devices/virtual/dmi/id/sys_vendor) $(cat /sys/devices/virtual/dmi/id/product_name) $(cat /sys/devices/virtual/dmi/id/product_version)"
	echo "cpu: $(lscpu | sed -n 's/^型号名称： *//p; s/^Model name: *//p' | head -1)"
	echo "governor: $(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor 2>/dev/null || echo unknown)"
	echo "epp: $(cat /sys/devices/system/cpu/cpufreq/policy0/energy_performance_preference 2>/dev/null || echo unknown)"
	echo "nproc: $(nproc)"
} | tee "$OUT/preflight.txt"

ORIG="$("$BIN" unlock get 2>/dev/null || echo error)"
if [ "$ORIG" = "error" ]; then
	echo "错误：无法读取 power_unlock 状态，见 $OUT/status-preflight.txt" >&2
	"$BIN" status >"$OUT/status-preflight.txt" 2>&1 || true
	cat "$OUT/status-preflight.txt" >&2
	exit 1
fi
echo "original power_unlock=$ORIG" | tee "$OUT/original.txt"

# Clean baseline: always start from balanced even if the EC remembered ON.
if [ "$ORIG" = "1" ]; then
	"$BIN" unlock off
	sleep 3
fi

snapshot pre
idle_test pre
load_test pre

log "enable Huawei performance mode"
"$BIN" unlock on
sleep 3
snapshot on
idle_test on
load_test on

log "restore balanced mode"
"$BIN" unlock off
sleep 3
snapshot post

if [ "$ORIG" = "1" ]; then
	log "restore original state (ON)"
	"$BIN" unlock on
fi

python3 "$ROOT/scripts/summarize.py" "$OUT" | tee "$OUT/summary.md"

log "done"
echo "raw data: $OUT"
