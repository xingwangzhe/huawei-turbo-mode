# Measurements

All numbers were collected on the unit described in the main README:

```text
HUAWEI BoF-XX-PCB / M1010
HUAWEI BoF-XX / M1010 / SKU C233 / MateBook D
BIOS 1.23 (2023-06-01)
Intel Core i7-1260P, 12C/16T
Ubuntu, kernel 7.0.0-42-generic
GNOME 50.1 / power-profiles-daemon 0.30
```

## 1. Firmware unlock

Before:

```text
Power unlock : OFF / balanced (WMI status=0 value=0)
MMIO RAPL    : PL1=20.000 W  PL2=60.375 W
EC[0x15]     : 0x20  (TUBO=0)
```

After `huawei-perf unlock on`:

```text
Power unlock : ON / performance (WMI status=0 value=1)
MMIO RAPL    : PL1=31.000 W  PL2=60.500 W
EC[0x15]     : 0x60  (TUBO=1)
```

The battery was at 22% and AC was online. The firmware's `STUB` precondition is
`ACIN == 1` and `RSOC >= 0x15` (21%); at 20% the command returned `status=1`.

## 2. EC derating

The EC's PL1/PL2 are dynamic. After the thermal test and at 23–26% battery while
charging, the same `TUBO=1` state reported:

```text
MMIO RAPL : PL1=10.000 W  PL2=35.000 W
Fan       : ~2695 RPM
CTMP      : 64–66°C
```

This is EC power/thermal policy, not a failed unlock; `TUBO` remains 1. A 60 s
16-thread burn at this 10 W limit stayed at 64–66°C with the fan at ~2695 RPM.

Temporarily lowering the charge threshold to 26% and restarting `thermald` did
not change PL1; the limit is EC-owned.

## 3. Fan response

| Scenario | EC PL1 | CTMP | Fan |
| --- | --- | --- | --- |
| Unlock burst | 31 W | 72°C | ~2757 RPM |
| 20 s all-core burn | ~31 W | 94°C | ~3.6–3.9k RPM |
| 60 s burn at current EC limit | 10 W | 64–66°C | 2695–2702 RPM |

WMI `0x0802` reads `FRHI/FRLO` at `0xFE8003CC`; `0x0A87` = 2695 RPM, matching
the WMI value. There is no fan/pwm hwmon and no ACPI `_FST/_FSL`. Manual fan
commands `0x0211` and `HADS 0x0618` return `status=1` on this unit; `GPFM/SPFM`
are no-op stubs. See [FAN.md](FAN.md).

## 4. 20-second burn caveat

| State | Avg package power | Avg frequency | Max temp | 7z total |
| --- | --- | --- | --- | --- |
| Balanced | 21.85 W | 2002 MHz | 94°C | 31862 |
| Performance | 13.20 W | 1364 MHz | 94°C | 13782 |

Both runs hit 94°C and the EC derated heavily. This is a thermal-saturation
result, not a reliable measure of sustained Turbo performance. A meaningful
comparison needs a cooler start, short bursts and controlled test order.

## 5. Persistence

Boot path (simulated with a service restart):

```text
$ systemctl is-enabled huawei-turbod.service
enabled
$ cat /var/lib/huawei-turbo/desired
on
$ sudo huawei-perf unlock off
回读：power_unlock=0（均衡模式）
$ sudo systemctl restart huawei-turbod.service
$ cat /run/huawei-turbo/status
{"desired":"on","actual":"on","ok":true,...}
$ sudo huawei-perf unlock get
1
```

Resume path:

```text
$ sudo huawei-perf unlock off
$ sudo /etc/systemd/system-sleep/huawei-turbo post
$ sudo huawei-perf unlock get
1
```

## 6. Generic D-Bus bridge

```text
$ gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \
    --object-path /org/xingwangzhe/HuaweiTurbo \
    --method org.xingwangzhe.HuaweiTurbo1.SetPerformance false
(true, '发送 HWMI 0xf04（power_unlock=0）...\n回读：power_unlock=0（均衡模式）')

$ gdbus call ... GetStatus
('off', 'off', true, 'already off (ppd:balanced)', '...')
$ powerprofilesctl get
balanced

$ gdbus call ... SetPerformance true
(true, 'already on (dbus)')
$ powerprofilesctl get
performance
```

PPD adapter:

```text
powerprofilesctl set balanced      -> desired=off actual=off
powerprofilesctl set performance   -> desired=on  actual=on
```

## English summary

- Real unlock verified: `TUBO=1`, MMIO PL1 20 W → 31 W, EC[0x15] 0x20 → 0x60.
- Firmware preconditions: AC online and battery ≥21%; otherwise `status=1`.
- EC power limits are dynamic; at low battery/thermal load it can report
  10 W/35 W while `TUBO` remains 1.
- Fan reads match EC `FRHI/FRLO`; the fan responds to temperature but this model
  exposes no writable fan interface.
- Persistence was verified via the same startup path (`systemctl restart`) and
  the sleep hook; the service is enabled at boot.
- Generic D-Bus `SetPerformance` and the optional PPD adapter both work.
