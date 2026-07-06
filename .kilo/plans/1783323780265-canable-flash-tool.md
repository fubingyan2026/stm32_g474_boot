# 修复: CAN FD 非 8/20 字节帧 DATA_END 解析错误

## 根因

`boot_fsm.c:handler_data_transfer` 的 `DATA_END` 处理中：

```c
*rem_len = (uint8_t)(msg->dlc - 4U);  // ← 假设帧长 = 头(4) + 剩余数据
```

CAN FD 帧的 DLC 是离散值（12, 16, 20, 24, ...），非实际数据长度。当 `build_data_end` 产出的帧长小于 `max_frame_size` 时，上位机 `_send` 用 `.ljust(frame_size, b'\x00')` 填充尾部，让实际传输帧比需要的更长。板端按 DLC 反推出的 `rem_len` 比真实剩余数据多出填充字节，导致：

```
block_accumulated_len + rem_len > 1024  →  memcpy 被跳过 → checksum 永远失败
```

验证：D=18（20 字节帧），56×18=1008，剩余=16，帧总长=4+16=20=恰好 max，无填充 → 通过 ✓

## 修复

### 1. `boot_fsm.c` — 限制 `rem_len` 不超出 Block 剩余空间

当前（line ~346）：
```c
if (rem_len > 0U) {
    if (ctx->block_accumulated_len + rem_len <= BOOT_BLOCK_SIZE) {
        memcpy(&ctx->ram_block_buffer[ctx->block_accumulated_len],
            remaining_data, rem_len);
        ctx->block_accumulated_len = (uint16_t)(ctx->block_accumulated_len + rem_len);
    }
}
```

改为按需截断：
```c
if (rem_len > 0U) {
    uint16_t free_space = (uint16_t)(BOOT_BLOCK_SIZE - ctx->block_accumulated_len);
    uint8_t copy_len = (rem_len < free_space) ? rem_len : (uint8_t)free_space;
    memcpy(&ctx->ram_block_buffer[ctx->block_accumulated_len],
        remaining_data, copy_len);
    ctx->block_accumulated_len = (uint16_t)(ctx->block_accumulated_len + copy_len);
}
```

### 2. （上位机，可选）`worker.py` — 修正 Block 拆分算法

当前 `frames_per_block` 用向上取整导致最后一个 DATA 帧可能超出 Block 边界。应改为：

```python
# 正确的帧数计算
frames_per_block = BLOCK_SIZE // d   # 完整 payload 帧数
remaining = BLOCK_SIZE - frames_per_block * d
```

与板端配合，帧少一帧但剩余数据用 DATA_END 尾帧承载，帧长恰好 = max_frame_size。

---

## 验证

编译并测试所有帧长（8, 12, 16, 20, 24, 32, 48, 64），期望全部通过协议传输。
