# 修复: 升级失败后 CANable 设备拒绝重新连接

## 问题

`worker.py` 的 `start_flash` 出错时，`finally` 块只调用 `bus.stop()` 不调 `bus.close()`。USB 设备保持 libusb 锁定，后续 `open()` 返回 `[Errno 13] Access denied`。Windows 上只能物理重插 CANable 恢复。

## 修复

### `worker.py` — 区分成败关闭 USB

```python
# start_flash 方法中, 在 finally 块改为:
success = False
try:
    self._run_flash(config)
    success = True
    self.finished.emit(True)
except Exception as e:
    ...
finally:
    if self._bus is not None:
        try:
            self._bus.stop()
            if not success:
                self._bus.close()   # 释放 USB, 用户可重连
                self._bus = None
        except Exception:
            pass
```

### `main_window.py` — 失败时 UI 恢复 (已有的修改确认生效)

`_on_flash_finished(success=False)` 中已调用 `self.device_panel.set_disconnected()`，无须额外改动。

## 验证

1. 故意触发升级失败 (如发无效 START/NACK)
2. 点 "连接" 按钮 → 应成功打开设备, 不再报 Access denied
