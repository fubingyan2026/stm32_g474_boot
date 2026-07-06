
---

# 基于双分区的 CAN / CAN FD 自适应固件升级设计方案书
*(针对 CAN FD 离散长度进行优化，校验位置固定化设计)*

## 1. 系统架构与 Flash 布局

为保障系统在升级中断或异常断电时不“变砖”，系统采用 **Bootloader + 双 App 分区（A/B 分区） + 引导配置区（Metadata）** 的架构。

### 1.1 Flash 物理划分
*   **Bootloader 区**：位于 Flash 起始位置，只读，不参与升级。负责校验并引导 App。
*   **App A 分区** / **App B 分区**：两个等大的运行/备用分区。
*   **Metadata（引导标志区）**：独立的 Flash 扇区，保存引导参数。

---

## 2. CAN / CAN FD 协议与帧格式定义

### 2.1 节点寻址
使用 11 位标准 ID（或根据需求转换为 29 位扩展 ID）进行点对点升级通信：
*   **`0x701` (Host -> Node)**：上位机发送给板卡的请求/数据。
*   **`0x702` (Node -> Host)**：板卡回复给上位机的应答/状态。

### 2.2 协议帧定义（离散长度自适应）
报文首部的控制开销固定占用 **2 字节**：
*   **Byte 0**：**命令字 (Command)**。
*   **Byte 1**：**包序号 (Sequence)**。对于非 DATA 帧，该字节填充 `0x00`。

根据底层硬件，双方在握手阶段协商**单帧物理总长度 (`Max_Frame_Size`)**。该值必须从支持的物理长度集合中选择：
*   **经典 CAN 支持**：`{ 8 }`（对应数据载荷为 6 字节）。
*   **CAN FD 支持**：`{ 8, 12, 16, 20, 24, 32, 48, 64 }` [^2.11]。

#### 2.2.1 核心命令帧定义

##### ① START (开始升级 - 上位机发送)
*   **Byte 0**：`0x01` (START)
*   **Byte 1**：`0x00` (预留)
*   **Byte 2-5**：固件总大小 (uint32)
*   **Byte 6-7**：硬件兼容 ID (uint16)
*   **Byte 8**：**单帧物理总长度 `Max_Frame_Size` (uint8)** ── *【从支持的离散长度中选择】*
    *   *经典 CAN* 设为 `8`。
    *   *CAN FD* 可设为 `64`（或 `32`, `48` 等）。
    *   *协商后的单帧数据载荷大小 $D = \text{Max\_Frame\_Size} - 2$*。

##### ② METADATA (元数据 - 上位机发送)
*   **Byte 0**：`0x02` (METADATA)
*   **Byte 1-4**：整包固件的全局 CRC32 校验码 (uint32)
*   **Byte 5-6**：版本号 (uint16)

##### ③ DATA (常规数据帧 - 上位机发送)
*   **Byte 0**：`0x03` (DATA)
*   **Byte 1**：包序号 (Sequence, 0~255)
*   **Byte 2 ~ (Max_Frame_Size - 1)**：传输 $D$ 字节数据。

##### ④ DATA_END (分块尾帧 - 上位机发送) ── *【关键防错设计】*
用于标识一个 1KB (1024 字节) 数据块的结束。**将校验和移至最前方的固定位置，彻底避开尾部填充干扰**：
*   **Byte 0**：`0x08` (DATA_END)
*   **Byte 1**：该分块最后一帧的序号 (Sequence)
*   **Byte 2-3**：**当前 1KB 块的 16位累加和校验码 Checksum (uint16)** ── *【固定在 Byte 2-3，不受 DLC 填充影响】*
*   **Byte 4 ~ 结束**：携带当前 Block 剩余的 $R$ 字节数据。
    *   *注：本帧的物理总长度同样对齐 `Max_Frame_Size`（如 64 字节），剩余未使用的字节由驱动/硬件自动填充，板卡解析时将直接忽略填充部分。*

##### ⑤ VERIFY (校验请求) 与 REBOOT (复位重启)
*   **Byte 0**：`0x04` (VERIFY) / `0x05` (REBOOT)

##### ⑥ ACK / NACK (板卡反馈)
*   **Byte 0**：`0x10` (ACK) / `0x11` (NACK)

---

## 3. 传输控制与自适应分块校验逻辑

### 3.1 1KB 数据块划分与填充计算
以 1KB (1024 字节) 物理页作为基本写入单位：

*   **场景 A：经典 CAN（`Max_Frame_Size` = 8，载荷 $D = 6$）**
    *   一个 Block 拆分为 171 帧。
    *   第 0 ~ 169 帧：`0x03` DATA，每帧含 6 字节数据（累计 1020 字节）。
    *   第 170 帧：`0x08` DATA_END，包含：
        *   `Byte 0-1`: 头信息
        *   `Byte 2-3`: Checksum
        *   `Byte 4-7`: 剩余 4 字节数据。整帧恰好 8 字节。
*   **场景 B：CAN FD 64（`Max_Frame_Size` = 64，载荷 $D = 62$）**
    *   一个 Block 拆分为 17 帧。
    *   第 0 ~ 15 帧：`0x03` DATA，每帧含 62 字节数据（累计 992 字节）。
    *   第 16 帧：`0x08` DATA_END，包含：
        *   `Byte 0-1`: 头信息
        *   `Byte 2-3`: Checksum
        *   `Byte 4-35`: 剩余 32 字节数据。
        *   `Byte 36-63`: 填充（0x00）。整帧 64 字节。

### 3.2 动态尾帧 Checksum 解析算法（板卡端）
板卡仅需要根据**已接收到的字节数**计算出剩余数据大小，直接从固定偏移位置提取数据和 Checksum：

```c
void on_data_end_received(can_msg_t msg) {
    // 1. Checksum 固定在 Byte 2 和 Byte 3，无视任何物理填充
    uint16_t expected_checksum = (msg.data[2] << 8) | msg.data[3];
    
    // 2. 根据 1KB 的边界，计算出该分块还剩多少字节没收
    uint16_t remaining_data_len = 1024 - current_block_accumulated_len; 
    
    // 3. 直接从 Byte 4 开始读取 remaining_data_len 长度的数据
    for (int i = 0; i < remaining_data_len; i++) {
        ram_block_buffer[current_block_accumulated_len++] = msg.data[4 + i];
    }
    // 此时，msg.data 中多余的填充字节（如 Byte 4+remaining_data_len 之后的部分）被安全忽略。

    // 4. 计算整个 1024 字节 RAM 缓存的累加和
    uint16_t calculated_checksum = 0;
    for (int i = 0; i < 1024; i++) {
        calculated_checksum += ram_block_buffer[i];
    }
    
    // 5. 校验与写入
    if (calculated_checksum == expected_checksum) {
        // 5.1 写入 Flash
        write_to_flash(ram_block_buffer, 1024);
        // 5.2 读回自校验
        if (read_and_compare_flash(ram_block_buffer, 1024)) {
            send_ack(0x08, STATUS_WRITE_SUCCESS);
            current_block_accumulated_len = 0; // 清空，准备下一个 Block
        } else {
            send_nack(0x08, ERR_FLASH_WRITE_VERIFY);
        }
    } else {
        send_nack(0x08, ERR_BLOCK_CHECKSUM); // 校验失败，触发 Host 重发当前 Block
    }
}
```

---

## 4. 方案优势总结

1.  **完美规避离散长度填充陷阱**：通过将 Checksum 固定在 `Byte 2-3`，剩余数据从 `Byte 4` 顺序向后排，板卡根据内部计数器截断读取。CAN FD 硬件由于对齐要求自动填充的无用数据（Padding）被完全隔离，彻底杜绝了校验失败。
2.  **协议结构高度统一**：经典 CAN 和 CAN FD 使用同一套命令解析逻辑，仅需在 `START` 阶段协商一次帧长，软件复用率极高。
3.  **双重物理安全闭环**：通过 **1KB Checksum（传输安全） + Flash 物理读回比对（存储安全）**，在不增加任何额外 CAN 帧交互的前提下，确保写入分区的固件没有任何单比特错误。