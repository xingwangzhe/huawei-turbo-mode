# bridge - generic desktop integration

**English** | [简体中文](README.zh-CN.md)

Desktop-agnostic persistence and integration: D-Bus, CLI, systemd, and an optional PPD adapter. GNOME/KDE are examples, not dependencies.

## Architecture

```text
desktop / widget / distro tool
        │  D-Bus: SetPerformance(bool)
        │  or CLI: huawei-turbo-ctl on/off
        ▼
huawei-turbod (root, systemd)
  ├─ /var/lib/huawei-turbo/desired       (persisted target)
  ├─ /run/huawei-turbo/status            (JSON status for UIs)
  ├─ systemd boot + sleep hook           (boot/resume restore)
  ├─ retry every 60 s                    (AC/RSOC precondition)
  └─ calls core/huawei-perf
        │
        ▼
core: debugfs → WMI WMAA → STUB/GTUB → EC TUBO
```

## D-Bus API

| Item | Value |
| --- | --- |
| Bus | system bus |
| Name | `org.xingwangzhe.HuaweiTurbo` |
| Object | `/org/xingwangzhe/HuaweiTurbo` |
| Interface | `org.xingwangzhe.HuaweiTurbo1` |

Methods:

| Method | Signature | Meaning |
| --- | --- | --- |
| `SetPerformance` | `b -> b, s` | `true` enables Turbo, `false` disables; returns `(ok, message)` |
| `GetStatus` | `-> s, s, b, s, s` | `(desired, actual, ok, message, updated)` |
| `GetInterfaceVersion` | `-> s` | currently `"1"` |

Examples:

```bash
gdbus call --system \
  --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance true

gdbus call --system \
  --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.GetStatus
```

Authorization: root and members of group `huawei-turbo` may write; reads are
public. The installer adds the installing user to the group and installs
`/etc/dbus-1/system.d/org.xingwangzhe.HuaweiTurbo.conf`.

## CLI

```bash
huawei-turbo-ctl on       # enable (pkexec)
huawei-turbo-ctl off      # disable (pkexec)
huawei-turbo-ctl status   # read /run/huawei-turbo/status
```

The CLI does not depend on D-Bus, so it works in scripts and minimal systems.

## State files

```text
/var/lib/huawei-turbo/desired    # on / off, survives reboot
/run/huawei-turbo/status         # JSON: desired, actual, ok, message, updated
```

## Configuration

`/etc/huawei-turbo/config.json`:

```json
{
  "follow_ppd": true,
  "retry_seconds": 60,
  "dbus": true
}
```

`follow_ppd` loads the optional PPD adapter; `retry_seconds` controls retries;
`dbus` enables the D-Bus service.

## PPD adapter (optional)

`bridge/sources/ppd.py` watches `net.hadess.PowerProfiles`:

```text
performance            -> turbo on
balanced / power-saver -> turbo off
```

It only uses PPD's D-Bus API, not GNOME-specific code. KDE with PPD works the
same way. If the desktop does not use PPD, set `follow_ppd` to `false` and use
D-Bus/CLI.

## Persistence

| Scenario | Mechanism |
| --- | --- |
| Boot/reboot | `huawei-turbod.service` reads `desired` at startup |
| Suspend/hibernate | `/etc/systemd/system-sleep/huawei-turbo` restarts the daemon in `post` |
| Desktop profile change | PPD adapter or D-Bus/CLI updates `desired` and applies |
| AC/RSOC precondition | status records the error; retries according to `retry_seconds` |
| Kernel/desktop restart | state is in `/var/lib`, independent of the session |

## Install

```bash
make
sudo ./scripts/install.sh
```

Uninstall:

```bash
sudo ./scripts/uninstall.sh          # keep state/config
sudo ./scripts/uninstall.sh --purge  # remove state/config too
```

## Examples

- [../examples/gnome/README.md](../examples/gnome/README.md): GNOME + PPD.
- [../examples/kde/README.md](../examples/kde/README.md): KDE + PPD or D-Bus/CLI.
- [../examples/generic/README.md](../examples/generic/README.md): any WM/DE.
