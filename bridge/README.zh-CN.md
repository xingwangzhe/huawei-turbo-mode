# bridge - generic desktop integration

[English](README.md) | **简体中文**

不绑定桌面的持久化与集成层：D-Bus、CLI、systemd，PPD 适配器可选。GNOME/KDE 是示例，不是依赖。

## 架构

```text
desktop / widget / distro tool
        │  D-Bus: SetPerformance(bool)
        │  or CLI: huawei-turbo-ctl on/off
        ▼
huawei-turbod (root, systemd)
  ├─ /var/lib/huawei-turbo/desired       （持久化目标状态）
  ├─ /run/huawei-turbo/status            （JSON 状态，供 UI 读取）
  ├─ systemd boot + sleep hook           （开机/唤醒恢复）
  ├─ huawei-rapl.path → huawei-rapl.service（按状态写 PL1/PL2）
  ├─ thermald drop-in                    （去掉 --adaptive）
  ├─ retry every 60 s                    （AC/RSOC 条件未满足时）
  └─ calls core/huawei-perf
        │
        ▼
core: debugfs → WMI WMAA → STUB/GTUB → EC TUBO
```

## D-Bus API

| 项 | 值 |
| --- | --- |
| 总线 | system bus |
| Name | `org.xingwangzhe.HuaweiTurbo` |
| Object | `/org/xingwangzhe/HuaweiTurbo` |
| Interface | `org.xingwangzhe.HuaweiTurbo1` |

方法：

| 方法 | 签名 | 说明 |
| --- | --- | --- |
| `SetPerformance` | `b -> b, s` | `true` 开启 Turbo，`false` 关闭；返回 `(ok, message)` |
| `GetStatus` | `-> s, s, b, s, s` | `(desired, actual, ok, message, updated)` |
| `GetInterfaceVersion` | `-> s` | 当前为 `"1"` |

示例：

```bash
gdbus call --system \
  --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.SetPerformance true

gdbus call --system \
  --dest org.xingwangzhe.HuaweiTurbo \
  --object-path /org/xingwangzhe/HuaweiTurbo \
  --method org.xingwangzhe.HuaweiTurbo1.GetStatus
```

权限：root 和 `huawei-turbo` 组成员可以写；任何用户都可以读。安装脚本会把
安装者加入该组，并安装
`/etc/dbus-1/system.d/org.xingwangzhe.HuaweiTurbo.conf`。

## CLI

```bash
huawei-turbo-ctl on       # 开启（pkexec）
huawei-turbo-ctl off      # 关闭（pkexec）
huawei-turbo-ctl status   # 读取 /run/huawei-turbo/status
```

CLI 不依赖 D-Bus，适合脚本和没有 D-Bus 集成的环境。

## 状态文件

```text
/var/lib/huawei-turbo/desired    # on / off，重启后仍保留
/run/huawei-turbo/status         # JSON：desired, actual, ok, message, updated
```

`status` 示例：

```json
{"desired":"on","actual":"on","ok":true,"message":"already on (startup)","updated":"2026-11-02T13:27:44+08:00"}
```

## RAPL 功耗墙

`huawei-turbod` 只切换 EC `TUBO`；PL1/PL2 由 `huawei-rapl.path` 监听
`/run/huawei-turbo/status` 后调用 `huawei-perf rapl` 应用：

| 状态 | PL1 | PL2 |
| --- | --- | --- |
| `on` | 31 W | 60.5 W（内核可能钳到 60.0 W） |
| `off` | 20 W | 60.375 W |

`thermald --adaptive` 会读取固件 DPTF passive policy 并把 PL1 压到 10 W；
`scripts/install.sh` 在检测到 thermald 时安装
`/etc/systemd/system/thermald.service.d/10-no-adaptive.conf` 去掉 `--adaptive`。

## 配置

默认路径 `/etc/huawei-turbo/config.json`：

```json
{
  "follow_ppd": true,
  "retry_seconds": 60,
  "dbus": true
}
```

| 键 | 作用 |
| --- | --- |
| `follow_ppd` | 是否加载 PPD 适配器（GNOME/KDE 使用 PPD 时开） |
| `retry_seconds` | AC/RSOC 条件未满足时的重试间隔 |
| `dbus` | 是否注册 D-Bus 服务 |

## PPD 适配器（可选）

`bridge/sources/ppd.py` 监听 `net.hadess.PowerProfiles` 的 `ActiveProfile`：

```text
performance            -> turbo on
balanced / power-saver -> turbo off
```

它只依赖 PPD 的 D-Bus 接口，不依赖 GNOME。KDE 如果也使用 PPD，同样适用。
如果桌面不用 PPD，把 `follow_ppd` 设为 `false`，改用 D-Bus/CLI。

## 持久化

| 场景 | 机制 |
| --- | --- |
| 开机/重启 | `huawei-turbod.service` 启动时读取 `desired` 并应用 |
| 休眠/睡眠恢复 | `/etc/systemd/system-sleep/huawei-turbo` 在 `post` 阶段重启 daemon |
| 桌面切换性能模式 | PPD 适配器或 D-Bus/CLI 更新 `desired` 并应用 |
| Turbo 状态变化 | `/run/huawei-turbo/status` 变化触发 `huawei-rapl.path`，写入 PL1/PL2 |
| AC/电量不满足 | 状态文件记录错误，按 `retry_seconds` 重试 |
| 内核/桌面重启 | 状态在 `/var/lib`，不依赖桌面会话 |

## 安装

```bash
make
sudo ./scripts/install.sh
```

安装脚本同时安装 `huawei-rapl.service`/`huawei-rapl.path`；如果检测到
thermald，还会安装去除 `--adaptive` 的 drop-in。

卸载：

```bash
sudo ./scripts/uninstall.sh          # 保留状态/配置
sudo ./scripts/uninstall.sh --purge  # 连状态/配置一起删除
```

## 示例

- [../examples/gnome/README.zh-CN.md](../examples/gnome/README.zh-CN.md)：GNOME + PPD。
- [../examples/kde/README.zh-CN.md](../examples/kde/README.zh-CN.md)：KDE + PPD 或 D-Bus/CLI。
- [../examples/generic/README.zh-CN.md](../examples/generic/README.zh-CN.md)：任意 WM/DE。
