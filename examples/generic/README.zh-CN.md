# Generic desktop example

[English](README.md) | **简体中文**

任意窗口管理器/桌面都可以通过 D-Bus 或 CLI 使用桥接，不依赖 GNOME/KDE。

## D-Bus

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

## CLI

```bash
huawei-turbo-ctl on
huawei-turbo-ctl off
huawei-turbo-ctl status
```

## 状态文件

```text
/var/lib/huawei-turbo/desired   # 持久化目标
/run/huawei-turbo/status        # JSON 状态
```

如果桌面不用 PPD，把 `/etc/huawei-turbo/config.json` 的 `follow_ppd` 设为
`false`，然后重启 `huawei-turbod.service`。
