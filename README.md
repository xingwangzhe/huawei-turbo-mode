# huawei-turbo-mode

[中文](#中文) | [English](#english)

[![CI](https://github.com/xingwangzhe/huawei-turbo-mode/actions/workflows/ci.yml/badge.svg)](https://github.com/xingwangzhe/huawei-turbo-mode/actions/workflows/ci.yml)
[![License: GPL-2.0](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](LICENSE)

Real Huawei EC Turbo unlock on Linux, plus a **desktop-agnostic** bridge.
The core is a hardware CLI; the bridge is D-Bus/CLI/systemd with an optional
PPD adapter. GNOME is only one example consumer.

Linux 下真正的华为 EC Turbo 解锁，外加一个**不绑定桌面环境**的桥接层。
核心是硬件 CLI；桥接层提供 D-Bus/CLI/systemd，PPD 只是可选适配器。
GNOME 只是示例消费者之一。

---

## 中文

### 两个问题，两层

| 层 | 目录 | 职责 |
| --- | --- | --- |
| **性能解锁核心** | [`core/`](core/README.md) | 直接调用华为 HWMI `STUB/GTUB`，切换 EC `TUBO`，不依赖桌面 |
| **通用桌面桥接** | [`bridge/`](bridge/README.md) | 持久化、D-Bus API、CLI、systemd、可选 PPD 适配器 |
| 桌面示例 | [`examples/`](examples/) | GNOME、KDE、任意 WM/DE 的接入示例 |
| 文档 | [`docs/`](docs/) | ACPI/测量/风扇诊断 |

核心只关心“怎么把固件里的性能模式打开”；桥接只关心“怎么让桌面、发行版、
脚本安全地调用它并记住状态”。GNOME 不是依赖，KDE 也不是。

### 已验证硬件（完整型号）

| 项目 | 值 |
| --- | --- |
| 主板厂商 | HUAWEI |
| 主板型号 | **BoF-XX-PCB** |
| 主板版本 | **M1010** |
| 整机型号 | **HUAWEI BoF-XX** |
| 整机版本 | M1010 |
| SKU | C233 |
| 产品家族 | MateBook D |
| 零售识别 | MateBook D15 2022（依据所有者已有 D15 仓库/博客；DMI 只写 BoF-XX） |
| BIOS | 1.23，2023-06-01 |
| CPU | 12th Gen Intel Core i7-1260P，12C/16T，Alder Lake-P |
| 测试内核 | 7.0.0-42-generic（Ubuntu） |
| WMI | GUID `ABBC0F5B-8EA1-11D1-A000-C90629100000`，`\_SB.HWMI.WMAA` |
| EC | `TUBO` = EC[0x15] bit6；`TDPS` = EC[0x1D] bit0；`ACIN` = EC[0x80] bit0 |

### 快速开始

```bash
make
sudo ./scripts/install.sh
```

安装后：

```bash
huawei-turbo-ctl status
huawei-turbo-ctl on
huawei-turbo-ctl off
```

通用 D-Bus（任何桌面/发行版工具都能调）：

```bash
gdbus call --system \
  --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance true
```

GNOME/KDE 如果使用 PPD，默认 `"follow_ppd": true` 会把 Performance 映射到
Turbo on，Balanced/Power Saver 映射到 off。

### 核心解锁（`core/`）

`huawei-perf` 走 in-tree `huawei-wmi` 保留的 debugfs：

```text
/sys/kernel/debug/huawei-wmi/{arg,call}
  → WMI WMAA → STUB/GTUB → EC TUBO/TDPS
```

关键点：

- `WMAA` 需要第二次调用才分发；
- `STUB` 检查 `ACIN == 1` 且 `RSOC >= 21%`；
- 命令：GET `0x0E04`，SET ON `0x10F04`，SET OFF `0x0F04`；
- 实测 `TUBO=1`、MMIO PL1 20 W → 31 W。

详细协议、ACPI 代码和完整型号见 [`core/README.md`](core/README.md)。

### 通用桥接（`bridge/`）

| 接口 | 说明 |
| --- | --- |
| D-Bus | `org.xingwangzhe.HuaweiTurbo` / `org.xingwangzhe.HuaweiTurbo1` |
| CLI | `huawei-turbo-ctl on/off/status` |
| 状态文件 | `/var/lib/huawei-turbo/desired`、`/run/huawei-turbo/status` |
| systemd | 开机启动 + 休眠恢复 |
| PPD 适配器 | 可选，`bridge/sources/ppd.py` |

D-Bus 写权限限 root 和 `huawei-turbo` 组；安装脚本会把安装者加入该组。
详细 API、配置和持久化机制见 [`bridge/README.md`](bridge/README.md)。

### 桌面示例

- GNOME：[`examples/gnome/README.md`](examples/gnome/README.md)（PPD，无需扩展）
- KDE：[`examples/kde/README.md`](examples/kde/README.md)（PPD 或 D-Bus/CLI）
- 任意 WM/DE：[`examples/generic/README.md`](examples/generic/README.md)

### 风扇

本机风扇由 EC 固件闭环控制，没有可写 fan/pwm 驱动；D16 2024 的
`0x0211`/`HADS` 在本机返回 `status=1`。只读诊断：

```bash
sudo ./core/build/huawei-perf fan-health 20
sudo ./core/scripts/fan-check.sh 20
```

详见 [`docs/FAN.md`](docs/FAN.md)。

### 测量与持久化

实测数据、热限制说明和持久化验证见 [`docs/MEASUREMENTS.md`](docs/MEASUREMENTS.md)。

### 安全与兼容性

- 不直接写 EC 寄存器；只走厂商 WMI。
- 固件的 AC/RSOC 检查和 EC 温度保护保持不变。
- 只在 BoF-XX-PCB / M1010 上验证；其他机型可能使用
  `\_SB.WMI1.WMAA` 或不同 opcode。
- 回滚：`huawei-turbo-ctl off`，或 `sudo ./scripts/uninstall.sh`，或关机重启。

### 参考

- [HelloZER0/HuaweiWmiControl](https://github.com/HelloZER0/HuaweiWmiControl)
- [baldandbrave/Huawei-WMI](https://github.com/baldandbrave/Huawei-WMI)
- [aymanbagabas/Huawei-WMI](https://github.com/aymanbagabas/Huawei-WMI)
- [qu1x/huawei-wmi](https://github.com/qu1x/huawei-wmi)
- [egormanga/gnome-extension-huawei-wmi](https://github.com/egormanga/gnome-extension-huawei-wmi)

## English

### Two problems, two layers

| Layer | Folder | Responsibility |
| --- | --- | --- |
| **Performance unlock core** | [`core/`](core/README.md) | Calls Huawei HWMI `STUB/GTUB` and toggles EC `TUBO`; no desktop dependency |
| **Generic desktop bridge** | [`bridge/`](bridge/README.md) | Persistence, D-Bus API, CLI, systemd, optional PPD adapter |
| Desktop examples | [`examples/`](examples/) | GNOME, KDE, generic WM/DE |
| Documentation | [`docs/`](docs/) | ACPI, measurements, fan diagnostics |

The core only handles "how to enable the firmware performance mode"; the bridge
handles "how desktops, distros and scripts call it safely and remember the
state". GNOME is not a dependency, and neither is KDE.

### Verified hardware (complete model)

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

### Quick start

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

### Core unlock (`core/`)

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

### Generic bridge (`bridge/`)

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

### Desktop examples

- GNOME: [`examples/gnome/README.md`](examples/gnome/README.md) (PPD, no extension)
- KDE: [`examples/kde/README.md`](examples/kde/README.md) (PPD or D-Bus/CLI)
- Generic: [`examples/generic/README.md`](examples/generic/README.md)

### Fan

The fan is closed-loop controlled by EC firmware; there is no writable
fan/pwm driver. The D16 2024 `0x0211`/`HADS` commands return `status=1` on this
unit. Read-only diagnostics:

```bash
sudo ./core/build/huawei-perf fan-health 20
sudo ./core/scripts/fan-check.sh 20
```

See [`docs/FAN.md`](docs/FAN.md).

### Measurements and persistence

See [`docs/MEASUREMENTS.md`](docs/MEASUREMENTS.md) for the measured PL1, EC bits,
thermal limits and the boot/resume persistence checks.

### Safety and compatibility

- No direct EC register writes; vendor WMI only.
- Firmware AC/RSOC checks and EC thermal protection remain active.
- Verified only on BoF-XX-PCB / M1010; other models may use
  `\_SB.WMI1.WMAA` or different opcodes.
- Rollback: `huawei-turbo-ctl off`, `sudo ./scripts/uninstall.sh`, or power-cycle.

### References

- [HelloZER0/HuaweiWmiControl](https://github.com/HelloZER0/HuaweiWmiControl)
- [baldandbrave/Huawei-WMI](https://github.com/baldandbrave/Huawei-WMI)
- [aymanbagabas/Huawei-WMI](https://github.com/aymanbagabas/Huawei-WMI)
- [qu1x/huawei-wmi](https://github.com/qu1x/huawei-wmi)
- [egormanga/gnome-extension-huawei-wmi](https://github.com/egormanga/gnome-extension-huawei-wmi)

### License

GPL-2.0. See [LICENSE](LICENSE).
