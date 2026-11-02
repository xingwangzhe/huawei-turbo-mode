# huawei-turbo-mode

**English** | [简体中文](README.zh-CN.md)

[![CI](https://github.com/xingwangzhe/huawei-turbo-mode/actions/workflows/ci.yml/badge.svg)](https://github.com/xingwangzhe/huawei-turbo-mode/actions/workflows/ci.yml)
[![License: GPL-2.0](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](LICENSE)

Real Huawei EC Turbo unlock on Linux, plus a desktop-agnostic bridge. The core is a hardware CLI; the bridge provides D-Bus/CLI/systemd with an optional PPD adapter. GNOME is only one example consumer.

## Two problems, two layers

| Layer | Folder | Responsibility |
| --- | --- | --- |
| **Performance unlock core** | [`core/`](core/README.md) | Calls Huawei HWMI `STUB/GTUB` and toggles EC `TUBO`; no desktop dependency |
| **Generic desktop bridge** | [`bridge/`](bridge/README.md) | Persistence, D-Bus API, CLI, systemd, optional PPD adapter |
| Desktop examples | [`examples/`](examples/) | GNOME, KDE, generic WM/DE |
| Documentation | [`docs/`](docs/) | ACPI, measurements, fan diagnostics |

The core only handles "how to enable the firmware performance mode"; the bridge
handles "how desktops, distros and scripts call it safely and remember the
state". GNOME is not a dependency, and neither is KDE.

## Verified hardware (complete model)

| Item | Value |
| --- | --- |
| Base board | **HUAWEI BoF-XX-PCB** |
| Board version | **M1010** |
| System product | **HUAWEI BoF-XX** |
| System version | M1010 |
| SKU | C233 |
| Family | MateBook D |
| Retail identification | MateBook D15 2022 (per owner's D15 repos/blog; DMI only says BoF-XX) |
| BIOS | 1.23, 2023-06-01 |
| CPU | 12th Gen Intel Core i7-1260P, 12C/16T, Alder Lake-P |
| Tested kernel | 7.0.0-42-generic (Ubuntu) |
| WMI | GUID `ABBC0F5B-8EA1-11D1-A000-C90629100000`, `\_SB.HWMI.WMAA` |
| EC | `TUBO` = EC[0x15] bit6; `TDPS` = EC[0x1D] bit0; `ACIN` = EC[0x80] bit0 |

## Quick start

```bash
make
sudo ./scripts/install.sh
```

Then:

```bash
huawei-turbo-ctl status
huawei-turbo-ctl on
huawei-turbo-ctl off
```

Generic D-Bus:

```bash
gdbus call --system \
  --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance true
```

With PPD (GNOME/KDE), the default `"follow_ppd": true` maps Performance to
Turbo on and Balanced/Power Saver to off.

## Core unlock (`core/`)

`huawei-perf` uses the in-tree `huawei-wmi` debugfs interface:

```text
/sys/kernel/debug/huawei-wmi/{arg,call}
  → WMI WMAA → STUB/GTUB → EC TUBO/TDPS
```

Key points:

- `WMAA` only dispatches on the second call;
- `STUB` requires `ACIN == 1` and `RSOC >= 21%`;
- commands: GET `0x0E04`, SET ON `0x10F04`, SET OFF `0x0F04`;
- measured: `TUBO=1`, MMIO PL1 20 W → 31 W.

Full protocol, ACPI excerpts and the complete model are in
[`core/README.md`](core/README.md).

## Generic bridge (`bridge/`)

| Interface | Description |
| --- | --- |
| D-Bus | `org.xingwangzhe.HuaweiTurbo` / `org.xingwangzhe.HuaweiTurbo1` |
| CLI | `huawei-turbo-ctl on/off/status` |
| State | `/var/lib/huawei-turbo/desired`, `/run/huawei-turbo/status` |
| systemd | boot + resume persistence |
| PPD adapter | optional, `bridge/sources/ppd.py` |

D-Bus writes are limited to root and group `huawei-turbo`; the installer adds
the installing user to the group. Full API/config/persistence details are in
[`bridge/README.md`](bridge/README.md).

## Desktop examples

- GNOME: [`examples/gnome/README.md`](examples/gnome/README.md) (PPD, no extension)
- KDE: [`examples/kde/README.md`](examples/kde/README.md) (PPD or D-Bus/CLI)
- Generic: [`examples/generic/README.md`](examples/generic/README.md)

## Fan

The fan is closed-loop controlled by EC firmware; there is no writable
fan/pwm driver. The D16 2024 `0x0211`/`HADS` commands return `status=1` on this
unit. Read-only diagnostics:

```bash
sudo ./core/build/huawei-perf fan-health 20
sudo ./core/scripts/fan-check.sh 20
```

See [`docs/FAN.md`](docs/FAN.md).

## Measurements and persistence

See [`docs/MEASUREMENTS.md`](docs/MEASUREMENTS.md) for the measured PL1, EC bits,
thermal limits and the boot/resume persistence checks.

## Safety and compatibility

- No direct EC register writes; vendor WMI only.
- Firmware AC/RSOC checks and EC thermal protection remain active.
- Verified only on BoF-XX-PCB / M1010; other models may use
  `\_SB.WMI1.WMAA` or different opcodes.
- Rollback: `huawei-turbo-ctl off`, `sudo ./scripts/uninstall.sh`, or power-cycle.

## References

- [HelloZER0/HuaweiWmiControl](https://github.com/HelloZER0/HuaweiWmiControl)
- [baldandbrave/Huawei-WMI](https://github.com/baldandbrave/Huawei-WMI)
- [aymanbagabas/Huawei-WMI](https://github.com/aymanbagabas/Huawei-WMI)
- [qu1x/huawei-wmi](https://github.com/qu1x/huawei-wmi)
- [egormanga/gnome-extension-huawei-wmi](https://github.com/egormanga/gnome-extension-huawei-wmi)

## License

GPL-2.0. See [LICENSE](LICENSE).
