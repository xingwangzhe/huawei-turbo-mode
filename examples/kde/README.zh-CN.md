# KDE example

[English](README.md) | **简体中文**

KDE Plasma 可以使用 PPD，也可以直接调用通用桥接。

## 方案 A：KDE 使用 PPD

如果系统安装了 `power-profiles-daemon`，保持默认：

```json
{"follow_ppd": true, "retry_seconds": 60, "dbus": true}
```

KDE 的电源模式切换 Performance/Balanced 时，桥接会自动映射到 EC Turbo。

## 方案 B：KDE 不用 PPD，直接调 D-Bus

把 `/etc/huawei-turbo/config.json` 改为：

```json
{"follow_ppd": false, "retry_seconds": 60, "dbus": true}
```

重启 daemon：

```bash
sudo systemctl restart huawei-turbod.service
```

然后给 KDE 全局快捷键或自定义部件绑定：

```bash
# 开
gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance true

# 关
gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance false

# 状态
gdbus call --system --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.GetStatus
```

也可以直接用 CLI：

```bash
huawei-turbo-ctl on
huawei-turbo-ctl off
huawei-turbo-ctl status
```

权限：当前用户需要在 `huawei-turbo` 组中（安装脚本会尝试加入），或者用
`pkexec` 运行 CLI。
