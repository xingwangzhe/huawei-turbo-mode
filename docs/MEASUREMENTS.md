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

## 2. The 10 W clamp: thermald --adaptive, not the EC

After the first unlock the same unit later reported PL1=10 W / PL2=35 W while
`TUBO` remained 1. This was originally attributed to EC policy, but a controlled
test showed it was `thermald --adaptive` applying the firmware DPTF passive
policy (`42A441D6-AE6A-462b-A84B-4A8CE79027D3`):

```text
$ sudo systemctl stop thermald
$ sudo huawei-perf rapl --pl1 31 --pl2 60.5 --apply
MMIO PL1 = 31.000 W                  # stays there
$ sudo /usr/sbin/thermald --systemd --dbus-enable --adaptive &
MMIO PL1 = 25.000 W -> 10.000 W      # reproduces the clamp
```

Starting thermald without `--adaptive` left 31 W / 60 W in place. A 15 s
16-thread AVX2 burn with non-adaptive thermald held PL1 at 31 W, reached 93°C,
and ran the EC fan at ~3630 RPM. No 10 W clamp occurred.

`TDPS` is written by `STUB`, but the EC does not keep it as a readable state:
with `TUBO=1`, EC[0x1D] read back `0x40` (TDPS=0) while PL1 was 31 W. Treat
TDPS as a transient/strobe, not as a condition for performance mode.

The repository now ships the fix:

- `10-no-adaptive.conf` removes `--adaptive` from thermald while keeping its
  default sensor monitoring;
- `huawei-rapl.service` + `huawei-rapl.path` apply PL1/PL2 from
  `/run/huawei-turbo/status` (`on` = 31 W / 60.5 W,
  `off` = 20 W / 60.375 W);
- `scripts/install.sh` installs and enables both when thermald is present.

The kernel may clamp PL2 to the RAPL maximum (60.0 W on this unit). The EC still
owns fan control and emergency throttling; 31 W reaches ~93–94°C under a
16-thread burn, which is the platform's thermal limit.

## 3. Fan response

| Scenario | EC PL1 | CTMP | Fan |
| --- | --- | --- | --- |
| Unlock burst | 31 W | 72°C | ~2757 RPM |
| 20 s all-core burn | ~31 W | 94°C | ~3.6–3.9k RPM |
| 15 s burn after fix | 31 W | 93°C | ~3630 RPM |
| 60 s burn before fix (adaptive thermald) | 10 W | 64–66°C | 2695–2702 RPM |

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

The Performance row above was collected while `thermald --adaptive` could still
clamp PL1 to 10 W, so it is not a clean 31 W run. After the fix, a 15 s
16-thread run held 31 W at 93°C; a new controlled comparison is still needed.

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

RAPL limits follow the same status file: `huawei-rapl.path` triggers
`huawei-rapl.service` on every status update, and `scripts/install.sh` enables
both. A `systemctl restart huawei-turbod` was verified to re-apply 31 W / 60 W.

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
- The 10 W/35 W state was caused by `thermald --adaptive` applying the DPTF
  passive policy, not by the EC. Non-adaptive thermald plus the RAPL apply
  service holds 31 W / 60 W (15 s 16-thread burn, 93°C, ~3630 RPM).
- `TUBO` is the persistent performance flag; `TDPS` read back 0 while PL1 was
  31 W and behaves as a transient.
- Fan reads match EC `FRHI/FRLO`; the fan responds to temperature but this model
  exposes no writable fan interface.
- Persistence was verified via the same startup path (`systemctl restart`) and
  the sleep hook; the services are enabled at boot.
- Generic D-Bus `SetPerformance` and the optional PPD adapter both work.
