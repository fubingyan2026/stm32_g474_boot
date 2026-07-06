"""CAN 升级协议状态机 — 在 QThread 中运行。"""
from __future__ import annotations

import time
import traceback
from typing import Optional

from PySide6.QtCore import QObject, Signal, Slot

from canable_sdk import ZDTCanable, CANFrame

from .protocol import (
    CAN_ID_HOST_TO_NODE, CAN_ID_NODE_TO_HOST, BLOCK_SIZE,
    CMD_START, CMD_METADATA, CMD_DATA, CMD_DATA_END, CMD_VERIFY, CMD_REBOOT,
    CMD_ACK, CMD_NACK,
    STATUS_BLOCK_CHECKSUM,
    build_start, build_metadata, build_data, build_data_end,
    build_verify, build_reboot, parse_header, parse_ack, parse_nack,
    compute_block_checksum, compute_crc32, block_chunks,
    frame_name, status_name,
)

BLOCK_ACK_TIMEOUT = 3.0
HANDSHAKE_TIMEOUT = 5.0
MAX_RETRIES = 3


class FlashConfig:
    def __init__(self, *, fw_path: str, bitrate: int = 1_000_000,
                 hw_compat_id: int = 1, version: int = 1,
                 fd_mode: bool = False, data_bitrate: int = 2_000_000,
                 max_frame_size: int = 8, serial: Optional[str] = None):
        self.fw_path = fw_path
        self.bitrate = bitrate
        self.hw_compat_id = hw_compat_id
        self.version = version
        self.fd_mode = fd_mode
        self.data_bitrate = data_bitrate
        self.max_frame_size = max_frame_size
        self.serial = serial


class FlashWorker(QObject):
    log_message = Signal(str)
    progress = Signal(int, int)  # current_block, total_blocks
    error_occurred = Signal(str)
    finished = Signal(bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._bus: Optional[ZDTCanable] = None
        self._cancel_requested = False

    @Slot()
    def cancel(self):
        self._cancel_requested = True

    @Slot(object)
    def start_flash(self, config: FlashConfig):
        self._cancel_requested = False
        self._config = config
        try:
            self._run_flash(config)
            self.finished.emit(True)
        except Exception as e:
            self.log_message.emit(f"[ERROR] {e}")
            self.error_occurred.emit(str(e))
            self.finished.emit(False)
        finally:
            self._close_bus()

    def _log(self, msg: str):
        self.log_message.emit(msg)

    def _open_bus(self, config: FlashConfig):
        self._log(f"[CAN] 扫描设备 (VID=0x1D50, PID=0x606F)...")
        devs = ZDTCanable.list_devices()
        if not devs:
            raise RuntimeError("未找到 CANable 设备")
        if config.serial:
            match = [d for d in devs if d.get("serial") == config.serial]
            if not match:
                raise RuntimeError(f"未找到 S/N={config.serial} 的设备")
            dev = match[0]
        else:
            dev = devs[0]

        self._log(f"[CAN] 连接: {dev.get('manufacturer','')} {dev.get('product','')} "
                  f"S/N={dev.get('serial','?')}")
        self._bus = ZDTCanable()
        self._bus.open()

        # 波特率 + FD
        if config.fd_mode:
            self._bus.fd_mode = True
            self._bus.set_data_bitrate(config.data_bitrate)
            self._bus.set_bitrate(config.bitrate)
            self._log(f"[CAN] FD 模式: 标称={config.bitrate}bps 数据相={config.data_bitrate}bps")
        else:
            self._bus.fd_mode = False
            self._bus.set_bitrate(config.bitrate)
            self._log(f"[CAN] 经典 CAN: {config.bitrate}bps")

        self._bus.start()
        time.sleep(0.1)
        # 排空 CANable 接收缓冲区中的残留帧（上次失败的 NACK/ACK 等）
        for _ in range(32):
            frame = self._bus.receive(timeout=0.05)
            if frame is None:
                break
            self._log(f"[DRAIN] 丢弃残留帧: {frame}")
        self._log(f"[CAN] 已启动, max_frame_size={config.max_frame_size}")

    def _close_bus(self):
        if self._bus is not None:
            try:
                self._bus.stop()
                self._bus.close()
            except Exception:
                pass
            self._bus = None

    def _send(self, data: bytes, frame_size: int):
        frame = CANFrame(
            can_id=CAN_ID_HOST_TO_NODE,
            data=data.ljust(frame_size, b'\x00'),
            extended=False,
            fd=frame_size > 8,
            brs=frame_size > 8,
        )
        self._bus.send(frame)

    def _recv(self, timeout: float) -> Optional[CANFrame]:
        end_time = time.monotonic() + timeout
        while time.monotonic() < end_time:
            if self._cancel_requested:
                raise RuntimeError("用户取消")
            remaining = end_time - time.monotonic()
            if remaining <= 0:
                break
            frame = self._bus.receive(timeout=min(remaining, 0.5))
            if frame is not None and frame.can_id == CAN_ID_NODE_TO_HOST:
                return frame
        return None

    def _wait_ack(self, expected_cmd: int, timeout: float = HANDSHAKE_TIMEOUT) -> bool:
        resp = self._recv(timeout)
        if resp is None:
            raise TimeoutError(f"等待 {frame_name(expected_cmd)} ACK 超时 ({timeout}s)")
        data = resp.data
        cmd, seq = parse_header(data)
        if cmd == CMD_NACK:
            n_cmd, code = parse_nack(data)
            self._log(f"[WARN] 收到非预期 NACK [{frame_name(n_cmd)}]: {status_name(code)}, 忽略并重试")
            return self._wait_ack(expected_cmd, timeout)
        if cmd != CMD_ACK:
            self._log(f"[WARN] 收到非预期帧: cmd=0x{cmd:02X}, 忽略")
            return self._wait_ack(expected_cmd, timeout)
        ack_cmd, status = parse_ack(data)
        if ack_cmd != expected_cmd:
            self._log(f"[WARN] 收到非预期 ACK: 期望 0x{expected_cmd:02X}, 收到 0x{ack_cmd:02X}, 忽略")
            return self._wait_ack(expected_cmd, timeout)
        return True

    def _wait_block(self) -> tuple[bool, Optional[CANFrame]]:
        """等待一个 DATA_END 的 ACK/NACK 回复。

        返回 (is_ack, frame_or_None)。
        """
        resp = self._recv(BLOCK_ACK_TIMEOUT)
        if resp is None:
            return False, None
        data = resp.data
        if len(data) == 0:
            return False, None
        if data[0] == CMD_ACK:
            return True, resp
        if data[0] == CMD_NACK:
            return False, resp
        return False, None

    def _run_flash(self, config: FlashConfig):
        self._log("=" * 50)
        self._log("开始固件升级流程")
        self._log("=" * 50)

        # 读取固件
        with open(config.fw_path, "rb") as f:
            fw_data = f.read()
        actual_fw_size = len(fw_data)
        self._log(f"[FW] 文件: {config.fw_path}")
        self._log(f"[FW] 大小: {actual_fw_size} 字节")
        fw_crc32 = compute_crc32(fw_data)
        self._log(f"[FW] CRC32: 0x{fw_crc32:08X}")

        # 拆分 Block
        blocks = block_chunks(fw_data, config.max_frame_size)
        total_blocks = len(blocks)
        self._log(f"[FW] Block 数: {total_blocks} (每块 {BLOCK_SIZE}B)")

        # 打开 CAN
        self._open_bus(config)

        # ── Phase 1: Handshake ──
        self._log(f"[PHASE 1] 握手: START")
        start_frame = build_start(actual_fw_size, config.hw_compat_id, config.max_frame_size)
        self._send(start_frame, 8)  # START 固定 8 字节
        self._wait_ack(CMD_START)
        self._log(f"[PHASE 1] 握手: START → ACK ✓")

        # 发送 METADATA
        meta_frame = build_metadata(fw_crc32, config.version)
        self._send(meta_frame, config.max_frame_size)
        self._wait_ack(CMD_METADATA)
        self._log(f"[PHASE 1] 握手: METADATA → ACK ✓")

        # ── Phase 2: Data Transfer ──
        self._log(f"[PHASE 2] 数据传输: {total_blocks} 个 Block")
        d = config.max_frame_size - 2

        for block_idx, block in enumerate(blocks):
            if self._cancel_requested:
                raise RuntimeError("用户取消")
            self.progress.emit(block_idx, total_blocks)

            frames_per_block = (BLOCK_SIZE + d - 1) // d
            self._log(f"[Block {block_idx+1}/{total_blocks}] "
                      f"{frames_per_block} 帧 (offset={block_idx * BLOCK_SIZE})")

            retries = 0
            while retries <= MAX_RETRIES:
                if retries > 0:
                    self._log(f"[Block {block_idx+1}] 重试 #{retries}")

                # 发送 DATA 帧
                for seq_idx in range(frames_per_block - 1):
                    chunk = block[seq_idx * d:(seq_idx + 1) * d]
                    raw = build_data(seq_idx, chunk)
                    self._send(raw, config.max_frame_size)
                    time.sleep(0.0001)
                    if self._cancel_requested:
                        raise RuntimeError("用户取消")

                # 发送 DATA_END
                last_start = (frames_per_block - 1) * d
                remaining = block[last_start:last_start + d]
                checksum = compute_block_checksum(block)
                end_raw = build_data_end(frames_per_block - 1, checksum, remaining)
                self._send(end_raw, config.max_frame_size)
                if self._cancel_requested:
                    raise RuntimeError("用户取消")

                # 等待 ACK/NACK
                ack, resp = self._wait_block()
                if ack:
                    self._log(f"[Block {block_idx+1}] → ACK ✓")
                    break
                else:
                    n_cmd = n_code = None
                    if resp is not None:
                        n_cmd, n_code = parse_nack(resp.data)
                    if n_code == STATUS_BLOCK_CHECKSUM:
                        self._log(f"[Block {block_idx+1}] NACK: CHECKSUM, 重试")
                        retries += 1
                        continue
                    if resp is None:
                        raise TimeoutError(f"Block {block_idx+1} 响应超时 ({BLOCK_ACK_TIMEOUT}s)")
                    raise RuntimeError(
                        f"Block {block_idx+1} NACK [{frame_name(n_cmd)}]: "
                        f"{status_name(n_code)} (0x{n_code:02X})")

        self.progress.emit(total_blocks, total_blocks)
        self._log("[PHASE 2] 数据传输完成 ✓")

        # ── Phase 3: Verify ──
        self._log("[PHASE 3] 校验: VERIFY")
        self._send(build_verify(), config.max_frame_size)
        self._wait_ack(CMD_VERIFY)
        self._log("[PHASE 3] CRC32 校验通过 ✓")

        # ── Phase 4: Reboot ──
        self._log("[PHASE 4] 提交: REBOOT")
        self._send(build_reboot(), config.max_frame_size)
        resp = self._recv(HANDSHAKE_TIMEOUT)
        if resp is not None and len(resp.data) > 0 and resp.data[0] == CMD_ACK:
            self._log("[PHASE 4] → ACK ✓")

        self._log("=" * 50)
        self._log(f"升级完成！({total_blocks} blocks, {actual_fw_size} bytes)")
        self._log("=" * 50)
