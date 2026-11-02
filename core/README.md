# core - Huawei EC Turbo unlock

This folder is the hardware layer. It is desktop-independent: a single CLI that
talks to the Huawei HWMI ACPI-WMI method and switches the EC Turbo state.

这个目录是硬件层，不依赖桌面环境：一个 CLI 直接调用华为 HWMI ACPI-WMI
方法，切换 EC Turbo。

## 中文

### 作用

`huawei-perf` 通过 in-tree `huawei-wmi` 驱动保留的 debugfs 调用口，向固件发送
华为私有 WMI 命令：

```text
/sys/kernel/debug/huawei-wmi/{arg,call}
        ↓
WMI GUID ABBC0F5B-8EA1-11D1-A000-C90629100000
        ↓
ACPI \_SB.HWMI.WMAA
        ↓
STUB / GTUB
        ↓
EC TUBO / TDPS
```

它不依赖 GNOME、KDE、PPD 或任何桌面服务；`bridge/` 只是它的上层封装。

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
| 电池 RSOC | EC 共享内存 `0xFE800200 + 0xE0`（16 位） |

### 协议

`WMAA` 的输入是 64 位小端；byte0 = MFID，byte1 = SFID，参数从 byte2 开始。
`WMAA` 内部有 `WMIC++`，只有第二次调用（`WMIC == 2`）才分发，所以调用方必须
重试一次。

| 命令 | 值 | 说明 |
| --- | --- | --- |
| GET Turbo | `0x00000E04` | `GTUB`，返回 `TUBO`，payload[1] == 1 表示开启 |
| SET ON | `0x00010F04` | `0x0F04 \| (1 << 16)` |
| SET OFF | `0x00000F04` | 参数为 0 |

固件前置条件：

```asl
Method (STUB, 1) {
    CreateByteField (Arg0, 0x02, TUBM)
    If (ACIN == 0)     { STAT = 1; Return }
    If (RSOC < 0x15)   { STAT = 1; Return }
    If (TUBM == 1) { TDPS = 1; TUBO = 1; _Q24() }
    If (TUBM == 0) { TDPS = 1; TUBO = 0; _Q23() }
}
```

因此：

- 必须插电（`ACIN == 1`）；
- 电池电量必须 ≥ 21%（`RSOC >= 0x15`）；
- 否则固件返回 `status=1`，不会写 `TUBO`。

### 构建与使用

```bash
make -C core
./core/build/huawei-perf status
sudo ./core/build/huawei-perf unlock on
sudo ./core/build/huawei-perf unlock off
sudo ./core/build/huawei-perf unlock get
```

常用命令：

| 命令 | 作用 |
| --- | --- |
| `status` | EC 状态、RAPL、风扇、温度 |
| `unlock on/off/get` | 切换/读取 Turbo |
| `fan [index]` | 读风扇转速 |
| `temp [zone]` | 读 EC 温度 |
| `power [seconds]` | RAPL 平均功耗 |
| `rapl [--pl1 W] [--pl2 W] [--apply]` | 查看/写 RAPL 功耗墙 |
| `fan-health [seconds]` | 风扇/温度健康诊断（只读） |
| `selftest` | 解析器自检（CI 用） |

### 实测结果

第一次成功解锁（电池 22%，AC 在线）：

| 状态 | power_unlock | EC TUBO | MMIO PL1 | MMIO PL2 |
| --- | --- | --- | --- | --- |
| 均衡 | 0 | 0 | 20.000 W | 60.375 W |
| 性能 | 1 | 1 | **31.000 W** | 60.500 W |

EC 位：`EC[0x15]` 从 `0x20` 变为 `0x60`（`TUBO=1`）。

注意：EC 的 PL1/PL2 是动态的。后续在电池 23%、热负载后，即使 `TUBO=1`，EC
也可能把 MMIO RAPL 降到 10 W / 35 W。这不是解锁失效，而是 EC 根据电池、适配器
和温度做的动态降额。完整数据见 [../docs/MEASUREMENTS.md](../docs/MEASUREMENTS.md)。

### 风扇

本机没有可写 fan/pwm 驱动，风扇由 EC 固件闭环控制；D16 2024 的 `0x0211`/`HADS`
在本机返回 `status=1`。只读诊断：

```bash
sudo ./core/build/huawei-perf fan-health 20
sudo ./core/scripts/fan-check.sh 20
```

详见 [../docs/FAN.md](../docs/FAN.md)。

### 安全

- 不直接写 EC 寄存器；只通过 in-tree `huawei-wmi` 的 debugfs 调用厂商 WMI。
- 固件的 AC/RSOC 检查和 EC 温度保护保持不变。
- 回滚：`unlock off`，或关机重启（EC 本身会复位）。
- 不要在未验证机型上盲跑：WMI 路径/命令可能不同。

## English

### What it does

`huawei-perf` uses the debugfs escape hatch kept by the in-tree `huawei-wmi`
driver to send the vendor HWMI command:

```text
/sys/kernel/debug/huawei-wmi/{arg,call}
        ↓
WMI GUID ABBC0F5B-8EA1-11D1-A000-C90629100000
        ↓
ACPI \_SB.HWMI.WMAA
        ↓
STUB / GTUB
        ↓
EC TUBO / TDPS
```

It does not depend on GNOME, KDE, PPD or any desktop service; `bridge/` is only
a layer on top.

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
| Battery RSOC | EC shared SRAM `0xFE800200 + 0xE0` (16-bit) |

### Protocol

The `WMAA` input is a 64-bit little-endian value: byte0 = MFID, byte1 = SFID,
arguments start at byte2. `WMAA` increments `WMIC` and only dispatches when
`WMIC == 2`, so callers must retry once.

| Command | Value | Meaning |
| --- | --- | --- |
| GET Turbo | `0x00000E04` | `GTUB`; returns `TUBO`, payload[1] == 1 means on |
| SET ON | `0x00010F04` | `0x0F04 \| (1 << 16)` |
| SET OFF | `0x00000F04` | argument 0 |

Firmware preconditions:

```asl
Method (STUB, 1) {
    CreateByteField (Arg0, 0x02, TUBM)
    If (ACIN == 0)     { STAT = 1; Return }
    If (RSOC < 0x15)   { STAT = 1; Return }
    If (TUBM == 1) { TDPS = 1; TUBO = 1; _Q24() }
    If (TUBM == 0) { TDPS = 1; TUBO = 0; _Q23() }
}
```

So: AC must be online, battery SoC must be at least 21%, otherwise the firmware
returns `status=1` and leaves `TUBO` unchanged.

### Build and use

```bash
make -C core
./core/build/huawei-perf status
sudo ./core/build/huawei-perf unlock on
sudo ./core/build/huawei-perf unlock off
sudo ./core/build/huawei-perf unlock get
```

Commands are the same as the Chinese table above (`status`, `unlock`,
`fan`, `temp`, `power`, `rapl`, `fan-health`, `selftest`).

### Measurements

First successful unlock (battery 22%, AC online): MMIO PL1 20.000 W → 31.000 W,
EC[0x15] 0x20 → 0x60 (`TUBO=1`). The EC later derated to 10 W / 35 W at lower
battery/thermal conditions; this is dynamic EC policy, not a failed unlock.
Full data: [../docs/MEASUREMENTS.md](../docs/MEASUREMENTS.md).

### Fan

There is no writable fan/pwm driver; the EC firmware owns the fan. The D16 2024
`0x0211`/`HADS` commands return `status=1` on this unit. Read-only diagnostics:

```bash
sudo ./core/build/huawei-perf fan-health 20
sudo ./core/scripts/fan-check.sh 20
```

See [../docs/FAN.md](../docs/FAN.md).

### Safety

- No direct EC register writes; only vendor WMI through the in-tree driver.
- Firmware AC/RSOC checks and EC thermal protection remain active.
- Rollback: `unlock off`, or power-cycle (the EC resets).
- Do not run on an unverified model: WMI paths/opcodes can differ.
