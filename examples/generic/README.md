# Generic desktop example

**English** | [简体中文](README.zh-CN.md)

Any WM/DE can use D-Bus or the CLI; no GNOME/KDE dependency.

```bash
gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance true

gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.GetStatus
```

Or:

```bash
huawei-turbo-ctl on
huawei-turbo-ctl off
huawei-turbo-ctl status
```

If the desktop does not use PPD, set `follow_ppd` to `false` in
`/etc/huawei-turbo/config.json` and restart `huawei-turbod.service`.
