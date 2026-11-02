# KDE example

**English** | [简体中文](README.zh-CN.md)

KDE Plasma can use PPD or call the generic bridge directly.

## Option A: KDE with PPD

Keep `"follow_ppd": true`; Plasma's Performance/Balanced power mode changes are
mapped to EC Turbo automatically.

## Option B: KDE without PPD

Set `"follow_ppd": false` in `/etc/huawei-turbo/config.json`, restart
`huawei-turbod.service`, then bind a KDE global shortcut or widget to:

```bash
gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance true

gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance false
```

Or use the CLI:

```bash
huawei-turbo-ctl on
huawei-turbo-ctl off
huawei-turbo-ctl status
```

The user must be in group `huawei-turbo` for D-Bus writes, or use the CLI via
`pkexec`.
