# GNOME example

**English** | [简体中文](README.zh-CN.md)

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
