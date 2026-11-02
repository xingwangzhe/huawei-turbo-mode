#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
huawei-turbod - generic desktop bridge for Huawei EC Turbo.

Core responsibilities:

  * persist the desired on/off state in /var/lib/huawei-turbo/desired
  * apply it at startup and retry when the firmware precondition fails
  * expose a D-Bus API (org.xingwangzhe.HuaweiTurbo) for any desktop
  * optionally follow power-profiles-daemon (GNOME/KDE/other PPD users)

The hardware call is performed by huawei-perf from the core/ component.
The bridge itself is desktop-agnostic: D-Bus and the CLI are the stable
interfaces; PPD is an optional adapter under bridge/sources/.
"""

import json
import os
import queue
import shutil
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime

STATE_DIR = os.environ.get("HUAWEI_TURBO_STATE_DIR", "/var/lib/huawei-turbo")
STATUS_DIR = os.environ.get("HUAWEI_TURBO_STATUS_DIR", "/run/huawei-turbo")
STATE_FILE = os.path.join(STATE_DIR, "desired")
STATUS_FILE = os.path.join(STATUS_DIR, "status")
CONFIG_FILE = os.environ.get(
    "HUAWEI_TURBO_CONFIG", "/etc/huawei-turbo/config.json"
)
PERF = os.environ.get("HUAWEI_PERF", "/usr/local/bin/huawei-perf")

DBUS_NAME = "org.xingwangzhe.HuaweiTurbo"
DBUS_PATH = "/org/xingwangzhe/HuaweiTurbo"
DBUS_IFACE = "org.xingwangzhe.HuaweiTurbo1"

GOOD_STATES = {"on", "off"}
STATE_LOCK = threading.RLock()

try:
    import dbus
    import dbus.service
    import dbus.mainloop.glib
    from gi.repository import GLib

    HAVE_DBUS = True
except Exception:  # optional desktop integration
    HAVE_DBUS = False


def now_iso():
    return datetime.now().astimezone().isoformat(timespec="seconds")


def log(message):
    print(f"{now_iso()} huawei-turbod: {message}", flush=True)


def _run(cmd, timeout=20):
    try:
        return subprocess.run(
            cmd,
            text=True,
            capture_output=True,
            timeout=timeout,
            check=False,
        )
    except FileNotFoundError as exc:
        return subprocess.CompletedProcess(cmd, 127, "", str(exc))
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(cmd, 124, "", "timed out")


def _ensure_dir(path):
    os.makedirs(path, mode=0o755, exist_ok=True)


def _atomic_write(path, text, mode=0o644):
    _ensure_dir(os.path.dirname(path))
    tmp = f"{path}.tmp.{os.getpid()}"
    with open(tmp, "w", encoding="utf-8") as handle:
        handle.write(text)
    os.chmod(tmp, mode)
    os.replace(tmp, path)


def load_config():
    config = {"follow_ppd": True, "retry_seconds": 60, "dbus": True}
    try:
        with open(CONFIG_FILE, encoding="utf-8") as handle:
            data = json.load(handle)
        if isinstance(data, dict):
            for key in config:
                if key in data:
                    config[key] = data[key]
    except FileNotFoundError:
        pass
    except Exception as exc:
        log(f"config {CONFIG_FILE}: {exc}; using defaults")

    config["follow_ppd"] = bool(config["follow_ppd"])
    config["dbus"] = bool(config["dbus"])
    try:
        config["retry_seconds"] = max(10, int(config["retry_seconds"]))
    except (TypeError, ValueError):
        config["retry_seconds"] = 60
    return config


def read_desired(path=None):
    path = path or STATE_FILE
    try:
        with open(path, "r", encoding="utf-8") as handle:
            value = handle.read().strip().lower()
    except OSError:
        return "off"
    if value in ("on", "1", "true", "performance"):
        return "on"
    if value in ("off", "0", "false", "balanced", "power-saver"):
        return "off"
    return "off"


def write_desired(state, path=None):
    if state not in GOOD_STATES:
        raise ValueError(f"invalid state: {state!r}")
    _atomic_write(path or STATE_FILE, f"{state}\n")


def write_status(desired, actual, ok, message, path=None):
    payload = {
        "desired": desired,
        "actual": actual,
        "ok": bool(ok),
        "message": (message or "")[-4000:],
        "updated": now_iso(),
    }
    _atomic_write(
        path or STATUS_FILE,
        json.dumps(payload, ensure_ascii=False) + "\n",
    )


def read_status():
    try:
        with open(STATUS_FILE, encoding="utf-8") as handle:
            data = json.load(handle)
        if isinstance(data, dict):
            return data
    except Exception:
        pass
    return {
        "desired": read_desired(),
        "actual": "unknown",
        "ok": False,
        "message": "no status yet",
        "updated": now_iso(),
    }


def get_actual(perf=None):
    result = _run([perf or PERF, "unlock", "get"], timeout=15)
    lines = result.stdout.strip().splitlines()
    if result.returncode == 0 and lines and lines[-1] in ("0", "1"):
        return "on" if lines[-1] == "1" else "off"
    return "unknown"


def get_ppd():
    tool = shutil.which("powerprofilesctl")
    if not tool:
        return None
    result = _run([tool, "get"], timeout=10)
    if result.returncode != 0:
        return None
    lines = result.stdout.strip().splitlines()
    return lines[-1] if lines else None


def set_ppd(profile):
    tool = shutil.which("powerprofilesctl")
    if not tool:
        return False
    if get_ppd() == profile:
        return True
    result = _run([tool, "set", profile], timeout=10)
    if result.returncode != 0:
        log(f"could not set PPD profile {profile}: {result.stderr.strip()}")
        return False
    return True


def sync_ppd(state):
    """Keep the desktop power-mode UI consistent with the EC state."""
    if state == "on":
        set_ppd("performance")
    elif get_ppd() == "performance":
        set_ppd("balanced")


def apply_state(state, reason, perf=None, sync=True):
    """Apply on/off and write the status file.  Returns True on success."""
    if state not in GOOD_STATES:
        write_status(state, "unknown", False, f"invalid state {state!r}")
        return False

    actual = get_actual(perf)
    if actual == state:
        write_status(state, actual, True, f"already {state} ({reason})")
        if sync:
            sync_ppd(state)
        log(f"{state}: already active ({reason})")
        return True

    result = _run([perf or PERF, "unlock", state], timeout=30)
    actual = get_actual(perf)
    ok = result.returncode == 0 and actual == state
    detail = (result.stdout + result.stderr).strip() or f"applied {state} ({reason})"
    write_status(state, actual, ok, detail)
    if ok:
        if sync:
            sync_ppd(state)
        log(f"{state}: applied ({reason})")
    else:
        log(f"{state}: failed ({reason}): {detail}")
    return ok


def set_and_apply(state, reason):
    """Atomically update the persisted state and apply it."""
    with STATE_LOCK:
        write_desired(state)
        return apply_state(state, reason)


def apply_current(reason):
    with STATE_LOCK:
        return apply_state(read_desired(), reason)


def load_ppd_monitor():
    try:
        from sources.ppd import ppd_monitor

        return ppd_monitor
    except Exception as exc:
        log(f"PPD adapter unavailable: {exc}")
        return None


def _caller_uid(sender):
    bus = dbus.SystemBus()
    dbus_obj = bus.get_object("org.freedesktop.DBus", "/org/freedesktop/DBus")
    iface = dbus.Interface(dbus_obj, "org.freedesktop.DBus")
    return int(iface.GetConnectionUnixUser(sender))


def caller_allowed(sender):
    """Allow root and members of the huawei-turbo group."""
    try:
        uid = _caller_uid(sender)
    except Exception:
        return False
    if uid == 0:
        return True
    try:
        import grp
        import pwd

        user = pwd.getpwuid(uid)
        group = grp.getgrnam("huawei-turbo")
        return group.gr_gid in os.getgrouplist(user.pw_name, user.pw_gid)
    except Exception:
        return False


if HAVE_DBUS:

    class TurboService(dbus.service.Object):
        """D-Bus control surface for desktop environments and distros."""

        def __init__(self, bus_name):
            super().__init__(bus_name, DBUS_PATH)

        @dbus.service.method(
            DBUS_IFACE,
            in_signature="b",
            out_signature="bs",
            sender_keyword="sender",
        )
        def SetPerformance(self, on, sender=None):
            if not caller_allowed(sender):
                raise dbus.exceptions.DBusException(
                    "not authorized; add the user to group huawei-turbo",
                    name="org.freedesktop.DBus.Error.AccessDenied",
                )
            state = "on" if bool(on) else "off"
            ok = set_and_apply(state, "dbus")
            return (dbus.Boolean(ok), dbus.String(read_status().get("message", "")))

        @dbus.service.method(DBUS_IFACE, in_signature="", out_signature="ssbss")
        def GetStatus(self):
            data = read_status()
            return (
                dbus.String(str(data.get("desired", "off"))),
                dbus.String(str(data.get("actual", "unknown"))),
                dbus.Boolean(bool(data.get("ok", False))),
                dbus.String(str(data.get("message", ""))),
                dbus.String(str(data.get("updated", ""))),
            )

        @dbus.service.method(DBUS_IFACE, in_signature="", out_signature="s")
        def GetInterfaceVersion(self):
            return dbus.String("1")


def start_dbus():
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    name = dbus.service.BusName(DBUS_NAME, bus=bus)
    service = TurboService(name)
    loop = GLib.MainLoop()
    thread = threading.Thread(target=loop.run, name="dbus", daemon=True)
    thread.start()
    log(f"D-Bus ready: {DBUS_NAME} {DBUS_PATH}")
    return service, loop


def run_once():
    _ensure_dir(STATE_DIR)
    _ensure_dir(STATUS_DIR)
    return 0 if apply_current("once") else 1


def main(argv):
    if "--once" in argv[1:]:
        return run_once()

    _ensure_dir(STATE_DIR)
    _ensure_dir(STATUS_DIR)
    config = load_config()
    desired = read_desired()
    log(
        f"starting (desired={desired}, perf={PERF}, "
        f"follow_ppd={config['follow_ppd']}, dbus={config['dbus']})"
    )
    apply_current("startup")

    stop = threading.Event()
    events = queue.Queue()

    def _stop(_signum, _frame):
        stop.set()

    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT, _stop)

    if HAVE_DBUS and config["dbus"]:
        try:
            start_dbus()
        except Exception as exc:
            log(f"D-Bus unavailable: {exc}")

    if config["follow_ppd"]:
        ppd_monitor = load_ppd_monitor()
        if ppd_monitor:
            threading.Thread(
                target=ppd_monitor,
                args=(events, stop),
                name="ppd-monitor",
                daemon=True,
            ).start()
    else:
        log("PPD adapter disabled by config")

    last_retry = 0.0
    while not stop.is_set():
        try:
            profile = events.get(timeout=5)
        except queue.Empty:
            profile = None

        if profile:
            if profile == "performance":
                new_state = "on"
            elif profile in ("balanced", "power-saver"):
                new_state = "off"
            else:
                new_state = None
            if new_state:
                set_and_apply(new_state, f"ppd:{profile}")

        now = time.monotonic()
        if now - last_retry >= config["retry_seconds"]:
            last_retry = now
            desired = read_desired()
            if desired != get_actual():
                apply_current("retry")

    log("stopping")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
