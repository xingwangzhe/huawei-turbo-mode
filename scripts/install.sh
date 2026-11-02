#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Install core/ (hardware unlock) and bridge/ (generic desktop integration).
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CORE="$ROOT/core"
BRIDGE="$ROOT/bridge"
PREFIX="${PREFIX:-/usr/local}"
LIBEXEC="$PREFIX/libexec/huawei-turbo"
STATE_DIR="/var/lib/huawei-turbo"
STATUS_DIR="/run/huawei-turbo"
CONFIG_DIR="/etc/huawei-turbo"

if [ "$(id -u)" -ne 0 ]; then
	echo "请用 root 运行：sudo $0 或 pkexec $0" >&2
	exit 1
fi

if [ ! -x "$CORE/build/huawei-perf" ]; then
	echo "构建产物不存在，请先在仓库根目录运行：make" >&2
	exit 1
fi

command -v python3 >/dev/null 2>&1 || {
	echo "缺少 python3" >&2
	exit 1
}
command -v gdbus >/dev/null 2>&1 || {
	echo "缺少 gdbus（glib2）；PPD 适配器将不可用" >&2
}

install -d "$PREFIX/bin" "$LIBEXEC/sources"
install -d /etc/systemd/system /etc/systemd/system-sleep
install -d /etc/dbus-1/system.d "$CONFIG_DIR"
install -d "$STATE_DIR" "$STATUS_DIR"
chmod 0755 "$STATE_DIR" "$STATUS_DIR"

# core: hardware unlock
install -m 0755 "$CORE/build/huawei-perf" "$PREFIX/bin/huawei-perf"
install -m 0755 "$CORE/build/burn" "$PREFIX/bin/huawei-perf-burn"

# bridge: generic daemon, CLI and optional PPD adapter
install -m 0755 "$BRIDGE/huawei_turbod.py" "$LIBEXEC/huawei-turbod"
install -m 0755 "$BRIDGE/huawei-turbo-set-state" "$LIBEXEC/huawei-turbo-set-state"
install -m 0755 "$BRIDGE/huawei-turbo-ctl" "$PREFIX/bin/huawei-turbo-ctl"
install -m 0644 "$BRIDGE/sources/__init__.py" "$LIBEXEC/sources/__init__.py"
install -m 0644 "$BRIDGE/sources/ppd.py" "$LIBEXEC/sources/ppd.py"
install -m 0644 "$BRIDGE/systemd/huawei-turbod.service" /etc/systemd/system/huawei-turbod.service
install -m 0755 "$BRIDGE/systemd/huawei-turbo-sleep" /etc/systemd/system-sleep/huawei-turbo
install -m 0644 "$BRIDGE/dbus/org.xingwangzhe.HuaweiTurbo.conf" \
	/etc/dbus-1/system.d/org.xingwangzhe.HuaweiTurbo.conf

# Bridge config: PPD is optional; D-Bus is the generic desktop API.
if [ ! -f "$CONFIG_DIR/config.json" ]; then
	if command -v powerprofilesctl >/dev/null 2>&1; then
		follow=true
	else
		follow=false
	fi
	printf '{\n  "follow_ppd": %s,\n  "retry_seconds": 60,\n  "dbus": true\n}\n' \
		"$follow" >"$CONFIG_DIR/config.json"
	chmod 0644 "$CONFIG_DIR/config.json"
fi

# Preserve the last saved state across upgrades.  On first install, mirror the
# current PPD policy so the daemon does not surprise the user.
if [ ! -f "$STATE_DIR/desired" ]; then
	if command -v powerprofilesctl >/dev/null 2>&1 &&
	   [ "$(powerprofilesctl get 2>/dev/null || true)" = "performance" ]; then
		printf 'on\n' >"$STATE_DIR/desired"
	else
		printf 'off\n' >"$STATE_DIR/desired"
	fi
	chmod 0644 "$STATE_DIR/desired"
fi

# D-Bus write access is limited to group huawei-turbo.  Add the installing
# desktop user so custom desktops can call SetPerformance without pkexec.
if ! getent group huawei-turbo >/dev/null 2>&1; then
	groupadd -r huawei-turbo 2>/dev/null || true
fi
target_user="${SUDO_USER:-}"
if [ -z "$target_user" ] && [ -n "${PKEXEC_UID:-}" ]; then
	target_user="$(id -nu "$PKEXEC_UID" 2>/dev/null || true)"
fi
if [ -z "$target_user" ]; then
	target_user="$(stat -c %U "$ROOT" 2>/dev/null || true)"
fi
if [ -n "$target_user" ] && [ "$target_user" != "root" ]; then
	usermod -aG huawei-turbo "$target_user" 2>/dev/null ||
		echo "警告：无法把 $target_user 加入 huawei-turbo 组；D-Bus 写入将只允许 root" >&2
fi

systemctl daemon-reload
systemctl reload dbus.service 2>/dev/null || true
systemctl enable huawei-turbod.service >/dev/null
systemctl restart huawei-turbod.service
sleep 1

echo "已安装："
echo "  core   : $PREFIX/bin/huawei-perf"
echo "  bridge : $PREFIX/bin/huawei-turbo-ctl"
echo "           $LIBEXEC/huawei-turbod"
echo "  units  : /etc/systemd/system/huawei-turbod.service"
echo "           /etc/systemd/system-sleep/huawei-turbo"
echo "  config : $CONFIG_DIR/config.json"
echo "  dbus   : org.xingwangzhe.HuaweiTurbo"
echo
echo "状态："
cat "$STATUS_DIR/status" 2>/dev/null || echo "(daemon 尚未写出状态)"
echo
echo "用法："
echo "  通用 CLI      : huawei-turbo-ctl on|off|status"
echo "  D-Bus         : gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \\"
echo "                  --object-path /org/xingwangzhe/HuaweiTurbo \\"
echo "                  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance true"
echo "  GNOME/KDE PPD : 选 Performance/Balanced（config follow_ppd=true 时）"
