# canable_sdk — CANable 2.5 驱动 SDK

Python USB-CAN 驱动，适用于 CANable 2.5 适配器（ElmueSoft 固件）。

## 快速开始

```python
from canable_sdk import ZDTCanable, CANFrame

with ZDTCanable() as bus:
    bus.set_bitrate(500_000)
    bus.start()
    frame = bus.receive(timeout=1.0)
    if frame:
        print(frame)
```

## 安装

```bash
pip install canable-sdk           # 仅驱动
pip install canable-sdk[gui]       # 驱动 + PySide6 GUI
```

Linux 需要 udev 规则免 root 访问：
```bash
sudo bash install_udev.sh && 重新插拔设备
```

Windows 需安装 WinUSB 驱动（ElmueSoft 固件支持自动安装）。

## 模块

| 模块 | 类/函数 | 说明 |
|------|---------|------|
| `driver.py` | `ZDTCanable` | 主驱动类：设备枚举、连接、位定时、收发 |
| `frame.py` | `CANFrame` | CAN 帧数据类 + 序列化/反序列化 |
| `protocol.py` | `_ElmueProtocol` | USB 协议流解析器（内部使用） |
| `constants.py` | 常量 | USB ID、协议码、标志枚举 |
| `bitrate.py` | 表 | 标称/数据相波特率时序表（160 MHz） |
| `cli.py` | `_cli()` | 命令行入口（直接运行模块） |

## 公开 API

`from canable_sdk import ZDTCanable, CANFrame, logger`

### ZDTCanable

| 方法 | 说明 |
|------|------|
| `list_devices()` | 列出所有 CANable 设备 |
| `open()` / `close()` | 打开/关闭设备连接 |
| `set_bitrate(bps)` | 设置标称波特率 |
| `set_data_bitrate(bps)` | 设置 CAN FD 数据相波特率 |
| `start(loopback=False)` | 启动 CAN 控制器 |
| `stop()` | 停止 CAN 控制器 |
| `recover()` | 从 bus-off 恢复 |
| `send(frame)` | 发送一帧 |
| `send_periodic(frame, interval_s, count)` | 周期发送 |
| `receive(timeout=1.0)` | 阻塞接收一帧 |
| `check_fd_support()` | 检测固件是否支持 CAN FD |
| `identify(duration_ms=1500)` | LED 闪烁识别 |
| `set_silent(enabled)` | 设置监听模式 |
| `set_filter(operation, can_id, mask)` | 硬件过滤器 |
| `get/set_termination(enabled)` | 终端电阻 |
| `on_receive(callback)` | 注册接收回调 |
| `start_listening()` | 启动后台监听线程 |

属性：
- `running` — 控制器是否已启动
- `fd_mode` — 是否启用 CAN FD 模式

### CANFrame

| 字段 | 类型 | 说明 |
|------|------|------|
| `can_id` | int | CAN ID |
| `data` | bytes | 数据 |
| `extended` | bool | 是否 29 位扩展帧 |
| `rtr` | bool | 远程帧 |
| `fd` | bool | CAN FD 帧 |
| `brs` | bool | 位速率切换 |
| `esi` | bool | 错误状态指示 |
| `timestamp` | float | 时间戳（秒） |
| `is_tx` | bool | 是否本机发送的回环帧 |

| 方法 | 说明 |
|------|------|
| `dlc` | 计算 DLC 码 |
| `dlc_to_len(dlc)` | DLC 转实际字节数 |

## FD 模式配置顺序

```python
bus.fd_mode = True                        # 1. 开启 FD
bus.set_bitrate(500_000)                  # 2. 标称波特率
bus.set_data_bitrate(2_000_000)            # 3. 数据相波特率（必须 > 标称以启用 BRS）
bus.start()                               # 4. 启动
```

⚠️ **关键约束**：
- `set_data_bitrate()` 必须在 `start()` **之前**调用
- 如果对端设备使用 BRS（数据波特率 > 标称），则 `data_bitrate` **必须设置得比标称大**，否则 FDCAN 硬件会丢弃 BRS 帧
- 数据相时序限制：TSEG1≤15, TSEG2≤15, SJW≤15（STM32G4 硬件限制）

## USB 标识

- VID = `0x1D50`，PID = `0x606F`
- 端点 IN = `0x81`，OUT = `0x02`

## 协议

支持两种协议：
- **ElmueSoft 变长协议**（自动检测）：消息头 `{size, msg_type}`，类型包括 RxFrame、TxEcho、Error、String、Busload
- **Legacy 固定 80 字节协议**：向后兼容旧固件

## 平台支持

- Linux：udev 规则 + libusb（预装）
- Windows：WinUSB 驱动（固件支持自动安装）
- macOS：需安装 libusb（`brew install libusb`）
