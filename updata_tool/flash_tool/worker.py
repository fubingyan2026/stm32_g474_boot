"""CAN 升级协议状态机 — 在 QThread 中运行。"""
from __future__ import annotations

import time
import traceback
from typing import Optional

from PySide6.QtCore import QObject, Signal, Slot

from canable_sdk import ZDTCanable, CANFrame

from .protocol import (
    BLOCK_SIZE,
    CMD_START, CMD_METADATA, CMD_DATA, CMD_DATA_START, CMD_DATA_END,
    CMD_VERIFY, CMD_REBOOT, CMD_ACK, CMD_NACK,
    STATUS_BLOCK_CHECKSUM, STATUS_BLOCK_INDEX_MISMATCH,
    STATUS_INVALID_FRAME, STATUS_TIMEOUT,
    build_start, build_metadata, build_data, build_data_start, build_data_end,
    build_verify, build_reboot, build_cancel, parse_header, parse_ack, parse_nack,
    parse_nack_block_index,
    compute_block_checksum, compute_checksum32, block_chunks,
    frame_name, status_name,
)

BLOCK_ACK_TIMEOUT = 3.0
HANDSHAKE_TIMEOUT = 5.0
MAX_RETRIES = 3
# 节点连续无响应超过该秒数即判定失联，直接中止（对齐板端 6s 全局会话超时）
NODE_LOST_TIMEOUT = 6.0

# 节点在这些错误码的 NACK 负载中回带期望块号，Host 据此重同步/断点续传
RESYNC_STATUS = (STATUS_BLOCK_INDEX_MISMATCH, STATUS_INVALID_FRAME, STATUS_TIMEOUT)


class FlashConfig:
    def __init__(self, *, fw_path: str, bitrate: int = 1_000_000,
                 hw_compat_id: int = 1, version: int = 1,
                 fd_mode: bool = False, data_bitrate: int = 2_000_000,
                 max_frame_size: int = 8, serial: Optional[str] = None,
                 can_id: int = 0x701):
        self.fw_path = fw_path
        self.bitrate = bitrate
        self.hw_compat_id = hw_compat_id
        self.version = version
        self.fd_mode = fd_mode
        self.data_bitrate = data_bitrate
        self.max_frame_size = max_frame_size
        self.serial = serial
        self.can_id = can_id          # Host→Node CAN ID
        self.can_id_resp = can_id + 1  # Node→Host CAN ID


class FlashWorker(QObject):
    log_message = Signal(str)
    progress = Signal(int, int)  # current_block, total_blocks
    error_occurred = Signal(str)
    finished = Signal(bool)
    bus_ready = Signal(object)  # 成功后传出 ZDTCanable 实例

    def __init__(self, parent=None):
        super().__init__(parent)
        self._bus: Optional[ZDTCanable] = None
        self._cancel_requested = False
        self._last_rx_monotonic = 0.0  # 最近一次收到节点响应的时刻（失联判定基准）
        self._config: Optional[FlashConfig] = None  # 启动前由主窗口设置

    @Slot()
    def cancel(self):
        self._cancel_requested = True

    @Slot()
    def start_flash(self):
        # 无参槽：config 在启动前由主窗口设为 self._config。
        # 必须以“绑定槽”方式连接 QThread.started（而非 lambda），否则会被派发回
        # GUI 线程执行，导致升级期间界面卡死、取消无法响应。
        # 注意：不要在此复位 _cancel_requested。每次升级都是新建的 FlashWorker，
        # __init__ 已置 False；若在此复位，会与 GUI 线程刚发出的 cancel() 竞态。
        config = self._config
        success = False
        try:
            self._run_flash(config)
            success = True
            self.bus_ready.emit(self._bus)
            self._bus = None  # 转让，不再自己关闭
            self.finished.emit(True)
        except Exception as e:
            self.log_message.emit(f"E MAIN: {e}")
            self.error_occurred.emit(str(e))
            self.finished.emit(False)
        finally:
            if self._bus is not None:
                try:
                    # 用户取消：先发一帧 CANCEL，令节点立即安全退回 IDLE（免等 6s 全局超时）
                    if self._cancel_requested and not success:
                        try:
                            self._send(build_cancel(), 8)
                            self._log_i("MAIN", "已发送 CANCEL，节点将退回 IDLE")
                        except Exception:
                            pass
                    self._bus.stop()
                    if not success:
                        self._bus.close()
                        self._bus = None
                except Exception:
                    pass

    def _log(self, msg: str):
        self.log_message.emit(msg)

    def _log_i(self, tag: str, msg: str):
        self.log_message.emit(f"I {tag}: {msg}")

    def _log_w(self, tag: str, msg: str):
        self.log_message.emit(f"W {tag}: {msg}")

    def _log_e(self, tag: str, msg: str):
        self.log_message.emit(f"E {tag}: {msg}")

    def _log_d(self, tag: str, msg: str):
        self.log_message.emit(f"D {tag}: {msg}")

    def _open_bus(self, config: FlashConfig):
        self._log_i("CAN", "扫描设备 (VID=0x1D50, PID=0x606F)...")
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

        self._log_i("CAN", f"连接: {dev.get('manufacturer','')} {dev.get('product','')} "
                    f"S/N={dev.get('serial','?')}")
        self._bus = ZDTCanable()
        self._bus.open()

        if config.fd_mode:
            self._bus.fd_mode = True
            self._bus.set_data_bitrate(config.data_bitrate)
            self._bus.set_bitrate(config.bitrate)
            self._log_i("CAN", f"FD 模式: 标称={config.bitrate}bps 数据相={config.data_bitrate}bps")
        else:
            self._bus.fd_mode = False
            self._bus.set_bitrate(config.bitrate)
            self._log_i("CAN", f"经典 CAN: {config.bitrate}bps")

        self._bus.start()
        time.sleep(0.1)
        for _ in range(32):
            frame = self._bus.receive(timeout=0.05)
            if frame is None:
                break
            self._log_d("CAN", f"丢弃残留帧: {frame}")
        self._log_i("CAN", f"已启动, max_frame_size={config.max_frame_size}")

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
            can_id=self._config.can_id,
            data=data.ljust(frame_size, b'\x00'),
            extended=False,
            fd=frame_size > 8,
            brs=False,
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
            # 0.2s 切片：无设备时也能在 ≤0.2s 内响应取消
            frame = self._bus.receive(timeout=min(remaining, 0.2))
            if frame is not None and frame.can_id == self._config.can_id_resp:
                self._last_rx_monotonic = time.monotonic()
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
            self._log_w("MAIN", f"收到非预期 NACK [{frame_name(n_cmd)}]: {status_name(code)}, 忽略并重试")
            return self._wait_ack(expected_cmd, timeout)
        if cmd != CMD_ACK:
            self._log_w("MAIN", f"收到非预期帧: cmd=0x{cmd:02X}, 忽略")
            return self._wait_ack(expected_cmd, timeout)
        ack_cmd, status = parse_ack(data)
        if ack_cmd != expected_cmd:
            self._log_w("MAIN", f"收到非预期 ACK: 期望 0x{expected_cmd:02X}, 收到 0x{ack_cmd:02X}, 忽略")
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

    def _poll_response(self) -> Optional[CANFrame]:
        """短超时探测一帧节点响应（DATA 发送途中即时拦截 + 兼作帧间节流）。

        使用 1ms 超时而非 0：底层 pyusb ``ep_in.read(timeout=0)`` 语义为“永久阻塞”，
        timeout=0 会导致无帧时挂死。1ms 非阻塞轮询既能捕获途中 NACK，又提供帧间节流。
        """
        frame = self._bus.receive(timeout=0.001)
        if frame is not None and frame.can_id == self._config.can_id_resp:
            self._last_rx_monotonic = time.monotonic()
            return frame
        return None

    def _reseek_index(self, data: Optional[bytes]) -> Optional[int]:
        """若为携带期望块号的重同步类 NACK，返回该期望块号，否则 None。"""
        if data is None or len(data) < 3 or data[0] != CMD_NACK:
            return None
        _cmd, code = parse_nack(data)
        if code in RESYNC_STATUS:
            return parse_nack_block_index(data)
        return None

    def _run_flash(self, config: FlashConfig):
        self._log_i("MAIN", "=" * 50)
        self._log_i("MAIN", "开始固件升级流程")
        self._log_i("MAIN", "=" * 50)

        # 读取固件
        with open(config.fw_path, "rb") as f:
            fw_data = f.read()
        actual_fw_size = len(fw_data)
        self._log_i("FW", f"文件: {config.fw_path}")
        self._log_i("FW", f"大小: {actual_fw_size} 字节")
        fw_checksum = compute_checksum32(fw_data)
        self._log_i("FW", f"Checksum32: 0x{fw_checksum:08X}")

        # 拆分 Block
        blocks = block_chunks(fw_data, config.max_frame_size)
        total_blocks = len(blocks)
        self._log_i("FW", f"Block 数: {total_blocks} (每块 {BLOCK_SIZE}B)")

        # 打开 CAN
        self._open_bus(config)
        self._last_rx_monotonic = time.monotonic()  # 失联判定起点

        # ── Phase 1: Handshake ──
        self._log_i("PHASE", "握手: START")
        start_frame = build_start(actual_fw_size, config.hw_compat_id, config.max_frame_size)
        self._send(start_frame, 8)  # START 固定 8 字节
        self._wait_ack(CMD_START)
        self._log_i("PHASE", "握手: START → ACK ✓")

        # 发送 METADATA
        meta_frame = build_metadata(fw_checksum, config.version)
        self._send(meta_frame, config.max_frame_size)
        self._wait_ack(CMD_METADATA)
        self._log_i("PHASE", "握手: METADATA → ACK ✓")

        # ── Phase 2: Data Transfer ──
        self._log_i("PHASE", f"数据传输: {total_blocks} 个 Block")
        d = config.max_frame_size - 2
        frames_per_block = (BLOCK_SIZE + d - 1) // d

        block_idx = 0
        attempt = 0
        while block_idx < total_blocks:
            if self._cancel_requested:
                raise RuntimeError("用户取消")
            block = blocks[block_idx]
            self.progress.emit(block_idx, total_blocks)

            # ── 块起始握手：DATA_START（每次尝试都发，据此重置板端接收状态）──
            self._send(build_data_start(block_idx), 8)  # DATA_START 固定 8 字节
            if self._cancel_requested:
                raise RuntimeError("用户取消")
            ack, resp = self._wait_block()
            if not ack:
                reseek = self._reseek_index(resp.data) if resp is not None else None
                if reseek is not None:
                    self._log_w("BLOCK", f"重同步: 板端期望块 {reseek}, 跳转寻址")
                    block_idx = reseek
                    attempt = 0
                    continue
                if resp is None:
                    # 节点无响应：以 6s 挂钟为失联界（对齐板端全局超时），超过即直接中止
                    if time.monotonic() - self._last_rx_monotonic > NODE_LOST_TIMEOUT:
                        raise TimeoutError(
                            f"节点失联超过 {NODE_LOST_TIMEOUT:.0f}s，已中止升级")
                    self._log_w("BLOCK", f"Block {block_idx} DATA_START 无响应, 重试重同步…")
                    continue
                n_cmd, n_code = parse_nack(resp.data)
                raise RuntimeError(
                    f"Block {block_idx} DATA_START NACK [{frame_name(n_cmd)}]: "
                    f"{status_name(n_code)} (0x{n_code:02X})")

            tag = f"{block_idx+1}/{total_blocks}"
            if attempt > 0:
                self._log_i("BLOCK", f"{tag} 重试 #{attempt}")
            self._log_i("BLOCK", f"{tag} DATA_START → ACK ✓, "
                        f"{frames_per_block} 帧 (offset={block_idx * BLOCK_SIZE})")

            # ── 发送 DATA 帧序列（途中非阻塞监听，丢包即时中止）+ DATA_END ──
            interrupted_to = None
            for seq_idx in range(frames_per_block - 1):
                chunk = block[seq_idx * d:(seq_idx + 1) * d]
                self._send(build_data(seq_idx, chunk), config.max_frame_size)
                # 1ms 短超时轮询：兼作帧间节流与丢包即时拦截
                poll = self._poll_response()
                reseek = self._reseek_index(poll.data) if poll is not None else None
                if reseek is not None:
                    _c, code = parse_nack(poll.data)
                    self._log_w("BLOCK", f"途中拦截 NACK [{status_name(code)}]: "
                                f"中止子帧, 重同步到块 {reseek}")
                    interrupted_to = reseek
                    break
                if self._cancel_requested:
                    raise RuntimeError("用户取消")

            if interrupted_to is not None:
                block_idx = interrupted_to
                attempt = 0
                continue

            last_start = (frames_per_block - 1) * d
            remaining = block[last_start:last_start + d]
            checksum = compute_block_checksum(block)
            end_raw = build_data_end(frames_per_block - 1, checksum, remaining)
            self._send(end_raw, config.max_frame_size)
            if self._cancel_requested:
                raise RuntimeError("用户取消")

            ack, resp = self._wait_block()
            if ack:
                self._log_i("BLOCK", f"{block_idx+1} → ACK ✓")
                block_idx += 1
                attempt = 0
                continue

            # DATA_END NACK / 超时处理
            reseek = self._reseek_index(resp.data) if resp is not None else None
            if reseek is not None:
                self._log_w("BLOCK", f"重同步: 板端期望块 {reseek}, 跳转寻址")
                block_idx = reseek
                attempt = 0
                continue
            n_cmd, n_code = (parse_nack(resp.data) if resp is not None else (None, None))
            if n_code == STATUS_BLOCK_CHECKSUM:
                attempt += 1
                if attempt > MAX_RETRIES:
                    raise RuntimeError(f"Block {block_idx+1} 校验失败重试超限 ({MAX_RETRIES} 次)")
                self._log_i("BLOCK", f"{block_idx+1} NACK: CHECKSUM, 重试")
                continue
            if resp is None:
                # 节点无响应：以 6s 挂钟为失联界，超过即直接中止
                if time.monotonic() - self._last_rx_monotonic > NODE_LOST_TIMEOUT:
                    raise TimeoutError(f"节点失联超过 {NODE_LOST_TIMEOUT:.0f}s，已中止升级")
                self._log_w("BLOCK", f"Block {block_idx+1} 无响应, 重发重同步…")
                continue
            raise RuntimeError(
                f"Block {block_idx+1} NACK [{frame_name(n_cmd)}]: "
                f"{status_name(n_code)} (0x{n_code:02X})")

        self.progress.emit(total_blocks, total_blocks)
        self._log_i("PHASE", "数据传输完成 ✓")

        self._log_i("PHASE", "校验: VERIFY")
        self._send(build_verify(), config.max_frame_size)
        self._wait_ack(CMD_VERIFY)
        self._log_i("PHASE", "校验和通过 ✓")

        self._log_i("PHASE", "提交: REBOOT")
        self._send(build_reboot(), config.max_frame_size)
        resp = self._recv(HANDSHAKE_TIMEOUT)
        if resp is not None and len(resp.data) > 0 and resp.data[0] == CMD_ACK:
            self._log_i("PHASE", "→ ACK ✓")
        else:
            raise TimeoutError(f"等待 REBOOT ACK 超时 ({HANDSHAKE_TIMEOUT}s)")

        self._log_i("MAIN", "=" * 50)
        self._log_i("MAIN", f"升级完成！({total_blocks} blocks, {actual_fw_size} bytes)")
        self._log_i("MAIN", "=" * 50)
