# bridge - generic desktop integration

This folder is desktop-agnostic. It persists the Huawei Turbo state and exposes
stable interfaces for any desktop environment or distribution:

- **D-Bus** for desktop widgets / settings panels / distro integration;
- **CLI** for scripts and users;
- **systemd** for boot and resume persistence;
- **optional PPD adapter** for GNOME/KDE or any desktop using
  `power-profiles-daemon`.

GNOME is only one example consumer. Nothing in this folder requires GNOME.

这个目录不绑定桌面环境。它负责持久化，并提供发行版可用的稳定接口：D-Bus、
CLI、systemd，以及可选的 PPD 适配器。GNOME 只是消费者之一。

## 中文

### 架构

```text
desktop / widget / distro tool
        │  D-Bus: SetPerformance(bool)
        │  or CLI: huawei-turbo-ctl on/off
        ▼
huawei-turbod (root, systemd)
  ├─ /var/lib/huawei-turbo/desired       （持久化目标状态）
  ├─ /run/huawei-turbo/status            （JSON 状态，供 UI 读取）
  ├─ systemd boot + sleep hook           （开机/唤醒恢复）
  ├─ retry every 60 s                    （AC/RSOC 条件未满足时）
  └─ calls core/huawei-perf
        │
        ▼
core: debugfs → WMI WMAA → STUB/GTUB → EC TUBO
```

### D-Bus API

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

### CLI

```bash
huawei-turbo-ctl on       # 开启（pkexec）
huawei-turbo-ctl off      # 关闭（pkexec）
huawei-turbo-ctl status   # 读取 /run/huawei-turbo/status
```

CLI 不依赖 D-Bus，适合脚本和没有 D-Bus 集成的环境。

### 状态文件

```text
/var/lib/huawei-turbo/desired    # on / off，重启后仍保留
/run/huawei-turbo/status         # JSON：desired, actual, ok, message, updated
```

`status` 示例：

```json
{"desired":"on","actual":"on","ok":true,"message":"already on (startup)","updated":"2026-11-02T13:27:44+08:00"}
```

### 配置

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

### PPD 适配器（可选）

`bridge/sources/ppd.py` 监听 `net.hadess.PowerProfiles` 的 `ActiveProfile`：

```text
performance            -> turbo on
balanced / power-saver -> turbo off
```

它只依赖 PPD 的 D-Bus 接口，不依赖 GNOME。KDE 如果也使用 PPD，同样适用。
如果桌面不用 PPD，把 `follow_ppd` 设为 `false`，改用 D-Bus/CLI。

### 持久化

| 场景 | 机制 |
| --- | --- |
| 开机/重启 | `huawei-turbod.service` 启动时读取 `desired` 并应用 |
| 休眠/睡眠恢复 | `/etc/systemd/system-sleep/huawei-turbo` 在 `post` 阶段重启 daemon |
| 桌面切换性能模式 | PPD 适配器或 D-Bus/CLI 更新 `desired` 并应用 |
| AC/电量不满足 | 状态文件记录错误，按 `retry_seconds` 重试 |
| 内核/桌面重启 | 状态在 `/var/lib`，不依赖桌面会话 |

### 安装

```bash
make
sudo ./scripts/install.sh
```

卸载：

```bash
sudo ./scripts/uninstall.sh          # 保留状态/配置
sudo ./scripts/uninstall.sh --purge  # 连状态/配置一起删除
```

### 示例

- [../examples/gnome/README.md](../examples/gnome/README.md)：GNOME + PPD。
- [../examples/kde/README.md](../examples/kde/README.md)：KDE + PPD 或 D-Bus/CLI。
- [../examples/generic/README.md](../examples/generic/README.md)：任意 WM/DE。

## English

### Architecture

```text
desktop / widget / distro tool
        │  D-Bus: SetPerformance(bool)
        │  or CLI: huawei-turbo-ctl on/off
        ▼
huawei-turbod (root, systemd)
  ├─ /var/lib/huawei-turbo/desired       (persisted target)
  ├─ /run/huawei-turbo/status            (JSON status for UIs)
  ├─ systemd boot + sleep hook           (boot/resume restore)
  ├─ retry every 60 s                    (AC/RSOC precondition)
  └─ calls core/huawei-perf
        │
        ▼
core: debugfs → WMI WMAA → STUB/GTUB → EC TUBO
```

### D-Bus API

| Item | Value |
| --- | --- |
| Bus | system bus |
| Name | `org.xingwangzhe.HuaweiTurbo` |
| Object | `/org/xingwangzhe/HuaweiTurbo` |
| Interface | `org.xingwangzhe.HuaweiTurbo1` |

Methods:

| Method | Signature | Meaning |
| --- | --- | --- |
| `SetPerformance` | `b -> b, s` | `true` enables Turbo, `false` disables; returns `(ok, message)` |
| `GetStatus` | `-> s, s, b, s, s` | `(desired, actual, ok, message, updated)` |
| `GetInterfaceVersion` | `-> s` | currently `"1"` |

Examples:

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

Authorization: root and members of group `huawei-turbo` may write; reads are
public. The installer adds the installing user to the group and installs
`/etc/dbus-1/system.d/org.xingwangzhe.HuaweiTurbo.conf`.

### CLI

```bash
huawei-turbo-ctl on       # enable (pkexec)
huawei-turbo-ctl off      # disable (pkexec)
huawei-turbo-ctl status   # read /run/huawei-turbo/status
```

The CLI does not depend on D-Bus, so it works in scripts and minimal systems.

### State files

```text
/var/lib/huawei-turbo/desired    # on / off, survives reboot
/run/huawei-turbo/status         # JSON: desired, actual, ok, message, updated
```

### Configuration

`/etc/huawei-turbo/config.json`:

```json
{
  "follow_ppd": true,
  "retry_seconds": 60,
  "dbus": true
}
```

`follow_ppd` loads the optional PPD adapter; `retry_seconds` controls retries;
`dbus` enables the D-Bus service.

### PPD adapter (optional)

`bridge/sources/ppd.py` watches `net.hadess.PowerProfiles`:

```text
performance            -> turbo on
balanced / power-saver -> turbo off
```

It only uses PPD's D-Bus API, not GNOME-specific code. KDE with PPD works the
same way. If the desktop does not use PPD, set `follow_ppd` to `false` and use
D-Bus/CLI.

### Persistence

| Scenario | Mechanism |
| --- | --- |
| Boot/reboot | `huawei-turbod.service` reads `desired` at startup |
| Suspend/hibernate | `/etc/systemd/system-sleep/huawei-turbo` restarts the daemon in `post` |
| Desktop profile change | PPD adapter or D-Bus/CLI updates `desired` and applies |
| AC/RSOC precondition | status records the error; retries according to `retry_seconds` |
| Kernel/desktop restart | state is in `/var/lib`, independent of the session |

### Install

```bash
make
sudo ./scripts/install.sh
```

Uninstall:

```bash
sudo ./scripts/uninstall.sh          # keep state/config
sudo ./scripts/uninstall.sh --purge  # remove state/config too
```

### Examples

- [../examples/gnome/README.md](../examples/gnome/README.md): GNOME + PPD.
- [../examples/kde/README.md](../examples/kde/README.md): KDE + PPD or D-Bus/CLI.
- [../examples/generic/README.md](../examples/generic/README.md): any WM/DE.
