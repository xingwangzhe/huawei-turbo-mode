#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Uninstall huawei-turbod and its units.
set -eu

PREFIX="${PREFIX:-/usr/local}"
LIBEXEC="$PREFIX/libexec/huawei-turbo"
PURGE=0

[ "${1:-}" = "--purge" ] && PURGE=1

if [ "$(id -u)" -ne 0 ]; then
	echo "请用 root 运行：sudo $0 [--purge]" >&2
	exit 1
fi

systemctl disable --now huawei-turbod.service 2>/dev/null || true
"$PREFIX/bin/huawei-perf" unlock off 2>/dev/null || true

rm -f /etc/systemd/system/huawei-turbod.service
rm -f /etc/systemd/system-sleep/huawei-turbo
rm -f /etc/dbus-1/system.d/org.xingwangzhe.HuaweiTurbo.conf
rm -f "$PREFIX/bin/huawei-perf" "$PREFIX/bin/huawei-perf-burn"
rm -f "$PREFIX/bin/huawei-turbo-ctl"
rm -rf "$LIBEXEC"
rm -f /run/huawei-turbo/status
rmdir /run/huawei-turbo 2>/dev/null || true
if [ "$PURGE" -eq 1 ]; then
	rm -rf /var/lib/huawei-turbo /etc/huawei-turbo
fi

systemctl daemon-reload
systemctl reload dbus.service 2>/dev/null || true
echo "已卸载。EC 状态已尝试恢复为均衡模式。"
