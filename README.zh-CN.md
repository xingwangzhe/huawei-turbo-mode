# huawei-turbo-mode

[English](README.md) | **简体中文**

[![CI](https://github.com/xingwangzhe/huawei-turbo-mode/actions/workflows/ci.yml/badge.svg)](https://github.com/xingwangzhe/huawei-turbo-mode/actions/workflows/ci.yml)
[![License: GPL-2.0](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](LICENSE)

Linux 下真正的华为 EC Turbo 解锁，外加一个不绑定桌面环境的桥接层。核心是硬件 CLI；桥接层提供 D-Bus/CLI/systemd，PPD 只是可选适配器。GNOME 只是示例消费者之一。

## 两个问题，两层

| 层 | 目录 | 职责 |
| --- | --- | --- |
| **性能解锁核心** | [`core/`](core/README.zh-CN.md) | 直接调用华为 HWMI `STUB/GTUB`，切换 EC `TUBO`，不依赖桌面 |
| **通用桌面桥接** | [`bridge/`](bridge/README.zh-CN.md) | 持久化、D-Bus API、CLI、systemd、可选 PPD 适配器 |
| 桌面示例 | [`examples/`](examples/) | GNOME、KDE、任意 WM/DE 的接入示例 |
| 文档 | [`docs/`](docs/) | ACPI/测量/风扇诊断 |

核心只关心“怎么把固件里的性能模式打开”；桥接只关心“怎么让桌面、发行版、
脚本安全地调用它并记住状态”。GNOME 不是依赖，KDE 也不是。

## 已验证硬件（完整型号）

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

## 快速开始

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

## 核心解锁（`core/`）

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

详细协议、ACPI 代码和完整型号见 [`core/README.zh-CN.md`](core/README.zh-CN.md)。

## 通用桥接（`bridge/`）

| 接口 | 说明 |
| --- | --- |
| D-Bus | `org.xingwangzhe.HuaweiTurbo` / `org.xingwangzhe.HuaweiTurbo1` |
| CLI | `huawei-turbo-ctl on/off/status` |
| 状态文件 | `/var/lib/huawei-turbo/desired`、`/run/huawei-turbo/status` |
| systemd | 开机启动 + 休眠恢复 |
| PPD 适配器 | 可选，`bridge/sources/ppd.py` |

D-Bus 写权限限 root 和 `huawei-turbo` 组；安装脚本会把安装者加入该组。
详细 API、配置和持久化机制见 [`bridge/README.zh-CN.md`](bridge/README.zh-CN.md)。

## 桌面示例

- GNOME：[`examples/gnome/README.zh-CN.md`](examples/gnome/README.zh-CN.md)（PPD，无需扩展）
- KDE：[`examples/kde/README.zh-CN.md`](examples/kde/README.zh-CN.md)（PPD 或 D-Bus/CLI）
- 任意 WM/DE：[`examples/generic/README.zh-CN.md`](examples/generic/README.zh-CN.md)

## 风扇

本机风扇由 EC 固件闭环控制，没有可写 fan/pwm 驱动；D16 2024 的
`0x0211`/`HADS` 在本机返回 `status=1`。只读诊断：

```bash
sudo ./core/build/huawei-perf fan-health 20
sudo ./core/scripts/fan-check.sh 20
```

详见 [`docs/FAN.md`](docs/FAN.md)。

## 测量与持久化

实测数据、热限制说明和持久化验证见 [`docs/MEASUREMENTS.md`](docs/MEASUREMENTS.md)。

## 安全与兼容性

- 不直接写 EC 寄存器；只走厂商 WMI。
- 固件的 AC/RSOC 检查和 EC 温度保护保持不变。
- 只在 BoF-XX-PCB / M1010 上验证；其他机型可能使用
  `\_SB.WMI1.WMAA` 或不同 opcode。
- 回滚：`huawei-turbo-ctl off`，或 `sudo ./scripts/uninstall.sh`，或关机重启。

## 参考

- [HelloZER0/HuaweiWmiControl](https://github.com/HelloZER0/HuaweiWmiControl)
- [baldandbrave/Huawei-WMI](https://github.com/baldandbrave/Huawei-WMI)
- [aymanbagabas/Huawei-WMI](https://github.com/aymanbagabas/Huawei-WMI)
- [qu1x/huawei-wmi](https://github.com/qu1x/huawei-wmi)
- [egormanga/gnome-extension-huawei-wmi](https://github.com/egormanga/gnome-extension-huawei-wmi)

## License

GPL-2.0，见 [LICENSE](LICENSE)。

