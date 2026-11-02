# GNOME example

GNOME is an **example consumer**, not a dependency of this project.

GNOME's built-in power mode uses `power-profiles-daemon` (PPD). With the default
bridge config (`"follow_ppd": true`), the PPD adapter maps:

```text
Performance -> EC Turbo on
Balanced    -> EC Turbo off
Power Saver -> EC Turbo off
```

No GNOME Shell extension is required. The old third-party extension that writes
`/sys/devices/platform/huawei-wmi/power_unlock` is not used, because mainline
`huawei-wmi` does not expose that file; the bridge uses D-Bus/PPD instead.

## 中文

GNOME 只是示例，不是依赖。

GNOME 自带的电源模式由 PPD 提供。保持默认配置 `"follow_ppd": true`，桥接会：

```text
Performance -> 打开 EC Turbo
Balanced    -> 关闭 EC Turbo
Power Saver -> 关闭 EC Turbo
```

不需要 GNOME Shell 扩展。旧第三方扩展依赖的
`/sys/devices/platform/huawei-wmi/power_unlock` 在 mainline 驱动里不存在；
本项目改用 PPD/D-Bus。

### 使用

1. 打开“设置 → 电源 → 电源模式”；
2. 选 Performance / Balanced / Power Saver；
3. 用 `huawei-turbo-ctl status` 或 D-Bus `GetStatus` 验证。

```bash
powerprofilesctl set performance
huawei-turbo-ctl status

powerprofilesctl set balanced
huawei-turbo-ctl status
```

如果不想让 GNOME 的电源模式控制华为 Turbo，把
`/etc/huawei-turbo/config.json` 里的 `follow_ppd` 改为 `false`，然后：

```bash
sudo systemctl restart huawei-turbod.service
```

之后用 D-Bus 或 `huawei-turbo-ctl on/off` 控制。

## English

GNOME is only an example consumer.

GNOME's built-in power mode is backed by PPD. With the default
`"follow_ppd": true`, the bridge maps Performance to Turbo on and
Balanced/Power Saver to Turbo off. No GNOME Shell extension is needed.

```bash
powerprofilesctl set performance
huawei-turbo-ctl status

powerprofilesctl set balanced
huawei-turbo-ctl status
```

To decouple GNOME's power mode from Huawei Turbo, set `follow_ppd` to `false`
in `/etc/huawei-turbo/config.json`, restart `huawei-turbod.service`, and use
D-Bus or `huawei-turbo-ctl on/off`.
