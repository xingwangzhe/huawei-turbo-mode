# 风扇诊断：BoF-XX / MateBook D15 2022

## 结论

这台机器的风扇由 **EC 固件闭环控制**，Linux 侧没有可写的 fan/pwm 驱动；
`huawei-wmi` 只提供只读的 WMI 转速查询。实测风扇本身能随温度变化，
但当前 EC 把包功耗限制在 **10 W**，CPU 不发热，所以风扇停在约 2.7k RPM。
这不是“风扇驱动坏了”，而是 EC 的供电/充电策略 + 自动风扇曲线的组合。

## 内核接口

```text
/sys/class/hwmon/hwmon0  ACAD
/sys/class/hwmon/hwmon1  acpitz
/sys/class/hwmon/hwmon2  BAT1
/sys/class/hwmon/hwmon3  nvme
/sys/class/hwmon/hwmon4  coretemp
/sys/class/hwmon/hwmon5  iwlwifi_1
```

没有 `fan*_input`，没有 `pwm*`；`/sys/class/thermal/thermal_zone*` 只有温度。
ACPI 里也没有 `_FST`/`_FSL`/`FAN` 设备。风扇控制完全在 EC 内部。

## 读取路径

WMI 命令 `0x0802`（MFID 2 / SFID 8）调用 ACPI `GFNS`：

```asl
Method (GFNS, 1) {
    FANN = byte2 of Arg0
    FANH = EC0.FRHI
    FANL = EC0.FRLO
    // fan 1: FRH2 / FRL2
}
```

EC 共享内存：

| 字段 | 地址 |
| --- | --- |
| ERAM base | `0xFE800300` |
| FRHI | `0xFE8003CC` |
| FRLO | `0xFE8003CD` |
| FRH2 | `0xFE8003CE` |
| FRL2 | `0xFE8003CF` |

实测：

```text
raw EC[0xCC..0xCF] = 0a 87 00 00
fan0 = (0x0A << 8) | 0x87 = 2695
WMI fan 0           = 2695-2702
```

WMI 读数和 EC 寄存器一致，说明转速读数没有坏。

## 风扇响应实测

| 场景 | EC PL1 | EC CTMP | 风扇 |
| --- | --- | --- | --- |
| 性能模式，刚解锁 | 31 W | 72°C | 2757 RPM |
| 20 秒全核 burn 后段 | 31 W 左右 | 94°C | 约 3.6–3.9k RPM |
| 当前（电池 25% 充电） | 10 W | 64–66°C | 2695–2702 RPM |
| 当前 60 秒 16 线程 burn | 10 W | 64–66°C | 2695–2702 RPM |

结论：风扇会响应温度；当前不升速是因为 EC 只给 10 W，热量不足以触发更高的风扇档位。
风扇曲线偏保守，但在 94°C 时确实拉到了 3.6k+ RPM。

## 手动风扇控制：本机型不支持

D16 2024 的社区方案使用：

```text
{0x06, 0x18, 0x02}                          # HADS，解锁 EC 写保护
{0x02, 0x11, fan, 0x01, speed_lo, speed_hi} # 手动风扇
```

本机实测：

```text
huawei-perf raw 0x211    -> status=1 (unsupported)
huawei-perf raw 0x618    -> status=1 (unsupported)
huawei-perf raw 0x20618  -> status=1 (unsupported)
```

本机 `WMAA` 的 MFID 2 只分发到 SFID 0x0E，没有 0x11；`HADS` 虽然在
`SSDT8` 里被引用，但运行时返回失败。`GPFM/SPFM`（0x1204/0x1304）在本机
DSDT 里是空实现，只返回 status=0，不写 EC。因此：

- 不能通过官方 WMI 做手动风扇曲线；
- 直接写 EC 寄存器风险很高（D16 社区记录过手动模式“硬持久化”，关机后风扇
  仍可能保持设定值，需要拔电+长按电源 20–30 秒恢复）；
- 不建议在没有该机型 EC 文档的情况下盲写。

## 建议

1. **先判断是不是真的风扇故障**：运行

   ```bash
   sudo ./core/scripts/fan-check.sh 20
   ```

   看 `verdict`：
   - `OK`：风扇在转，且温度变化时转速有响应；
   - `STALLED`：温度 >55°C 但转速为 0，检查排线/风扇硬件；
   - `NO_RESPONSE`：温度变化 ≥10°C 但转速变化 <100 RPM，可能是曲线/硬件；
   - `NO_TACH`：WMI 读不到转速，检查 `huawei_wmi`/debugfs。

2. **看 EC 是否在低功耗降额**：`huawei-perf status` 里的 `MMIO PL1`。
   当前电池 25% 充电时是 10 W；插电并把电量充高后，EC 可能恢复到 20–31 W，
   风扇会随热量上升。

3. **保持性能模式**：`huawei-turbod` 已把 GNOME Performance 映射到 `TUBO=1`，
   EC 会在允许时提高 PL1，风扇随之升速。

4. **硬件维护**：如果风扇在 90°C+ 仍只有 3k RPM、或轴噪/异响，清理风扇和
   散热鳍片、检查硅脂；这是薄本在 31 W 下 94°C 的常见瓶颈。

5. **不要盲写 EC**：本机型没有可用的手动风扇命令；直接改 EC 可能让风扇
   进入不可恢复的手动模式。若确实需要，必须先有该机型的 EC 寄存器文档和
   自动恢复/看门狗方案。

## 可选：只读 hwmon

如果只是希望 `sensors` 能看到风扇，可以后续加一个只读 hwmon 驱动，把
`GFNS` 的转速和 `GTMP` 的温度暴露成 `fan1_input`/`temp1_input`。它不会改变
风扇行为，也不会解决散热，只是可观测性；当前用 `huawei-perf fan-health`
已经能完成同样的诊断。
