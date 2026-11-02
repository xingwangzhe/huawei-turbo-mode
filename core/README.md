# core - Huawei EC Turbo unlock

**English** | [简体中文](README.zh-CN.md)

Hardware layer: direct Huawei HWMI `STUB/GTUB` unlock, independent of any desktop environment.

## What it does

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
| Battery RSOC | EC shared SRAM `0xFE800200 + 0xE0` (16-bit) |

## Protocol

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

## Build and use

```bash
make -C core
./core/build/huawei-perf status
sudo ./core/build/huawei-perf unlock on
sudo ./core/build/huawei-perf unlock off
sudo ./core/build/huawei-perf unlock get
```

Commands are the same as the Chinese table above (`status`, `unlock`,
`fan`, `temp`, `power`, `rapl`, `fan-health`, `selftest`).

## Measurements

First successful unlock (battery 22%, AC online): MMIO PL1 20.000 W → 31.000 W,
EC[0x15] 0x20 → 0x60 (`TUBO=1`). The EC later derated to 10 W / 35 W at lower
battery/thermal conditions; this is dynamic EC policy, not a failed unlock.
Full data: [../docs/MEASUREMENTS.md](../docs/MEASUREMENTS.md).

## Fan

There is no writable fan/pwm driver; the EC firmware owns the fan. The D16 2024
`0x0211`/`HADS` commands return `status=1` on this unit. Read-only diagnostics:

```bash
sudo ./core/build/huawei-perf fan-health 20
sudo ./core/scripts/fan-check.sh 20
```

See [../docs/FAN.md](../docs/FAN.md).

## Safety

- No direct EC register writes; only vendor WMI through the in-tree driver.
- Firmware AC/RSOC checks and EC thermal protection remain active.
- Rollback: `unlock off`, or power-cycle (the EC resets).
- Do not run on an unverified model: WMI paths/opcodes can differ.
