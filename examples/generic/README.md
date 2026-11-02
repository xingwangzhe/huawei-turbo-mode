# Generic desktop example

Any WM/DE can use the bridge through D-Bus or the CLI. Nothing here depends on
GNOME or KDE.

## 中文

任意窗口管理器/桌面都可以通过 D-Bus 或 CLI 使用桥接，不依赖 GNOME/KDE。

### D-Bus

```bash
gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance true

gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance false

gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.GetStatus
```

### CLI

```bash
huawei-turbo-ctl on
huawei-turbo-ctl off
huawei-turbo-ctl status
```

### 状态文件

```text
/var/lib/huawei-turbo/desired   # 持久化目标
/run/huawei-turbo/status        # JSON 状态
```

如果桌面不用 PPD，把 `/etc/huawei-turbo/config.json` 的 `follow_ppd` 设为
`false`，然后重启 `huawei-turbod.service`。

## English

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
