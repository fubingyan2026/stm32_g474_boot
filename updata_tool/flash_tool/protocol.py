"""Bootloader 协议帧编解码 — 纯 Python 数据变换，无 Qt 依赖。

对应 boot_protocol_spec.md §4，所有帧基于标准 CAN 11-bit ID。
"""
from __future__ import annotations

import struct
from typing import Optional

# ── CAN ID ──
CAN_ID_HOST_TO_NODE = 0x701
CAN_ID_NODE_TO_HOST = 0x702

# ── 命令字 ──
CMD_START = 0x01
CMD_METADATA = 0x02
CMD_DATA = 0x03
CMD_VERIFY = 0x04
CMD_REBOOT = 0x05
CMD_DATA_END = 0x08
CMD_ACK = 0x10
CMD_NACK = 0x11

# ── NACK 状态码 ──
STATUS_OK = 0x00
STATUS_BLOCK_CHECKSUM = 0x01
STATUS_FLASH_WRITE_ERR = 0x02
STATUS_FLASH_VERIFY_ERR = 0x03
STATUS_CRC32_ERR = 0x04
STATUS_INVALID_FRAME = 0x05
STATUS_INVALID_STATE = 0x06
STATUS_TIMEOUT = 0x07
STATUS_HW_MISMATCH = 0x08
STATUS_FLASH_ERASE_ERR = 0x09
STATUS_FLASH_READ_ERR = 0x0A
STATUS_FRAME_SIZE = 0x0B
STATUS_FW_TOO_BIG = 0x0C

STATUS_NAMES = {
    0x00: "OK",
    0x01: "BLOCK_CHECKSUM",
    0x02: "FLASH_WRITE_ERR",
    0x03: "FLASH_VERIFY_ERR",
    0x04: "CRC32_ERR",
    0x05: "INVALID_FRAME",
    0x06: "INVALID_STATE",
    0x07: "TIMEOUT",
    0x08: "HW_MISMATCH",
    0x09: "FLASH_ERASE_ERR",
    0x0A: "FLASH_READ_ERR",
    0x0B: "FRAME_SIZE",
    0x0C: "FW_TOO_BIG",
}

CMD_NAMES = {
    0x01: "START",
    0x02: "METADATA",
    0x03: "DATA",
    0x04: "VERIFY",
    0x05: "REBOOT",
    0x08: "DATA_END",
    0x10: "ACK",
    0x11: "NACK",
}

# ── 协议常量 ──
FRAME_HEADER_LEN = 2
BLOCK_SIZE = 1024
SUPPORTED_FRAME_SIZES = {8, 12, 16, 20, 24, 32, 48, 64}


def is_valid_frame_size(size: int) -> bool:
    return size in SUPPORTED_FRAME_SIZES


def payload_size(max_frame_size: int) -> int:
    return max_frame_size - FRAME_HEADER_LEN


def frame_name(cmd: int) -> str:
    return CMD_NAMES.get(cmd, f"0x{cmd:02X}")


def status_name(st: int) -> str:
    return STATUS_NAMES.get(st, f"0x{st:02X}")


# ── 帧构造器 ──

def build_start(fw_size: int, hw_id: int, max_frame_size: int) -> bytes:
    """构造 START 帧 (8 字节)。

    [0x01][fw_size(4B BE)][hw_id(2B BE)][max_frame_size]
    """
    if not is_valid_frame_size(max_frame_size):
        raise ValueError(f"invalid max_frame_size={max_frame_size}")
    if not (0 < fw_size <= 0xFFFFFFFF):
        raise ValueError(f"fw_size out of range: {fw_size}")
    return struct.pack(">BIHB", CMD_START, fw_size, hw_id, max_frame_size)
    # B=1, I=4, H=2, B=1 → 8 bytes


def build_metadata(crc32: int, version: int) -> bytes:
    """构造 METADATA 帧 (7 字节，调用方填充到 8 字节)。

    [0x02][crc32(4B BE)][version(2B BE)]
    """
    return struct.pack(">BIH", CMD_METADATA, crc32, version)
    # B=1, I=4, H=2 → 7 bytes


def build_data(seq: int, payload: bytes) -> bytes:
    """构造 DATA 帧。

    [0x03][seq(1B)][payload]
    """
    return struct.pack(">BB", CMD_DATA, seq) + payload


def build_data_end(seq: int, checksum: int, remaining_data: bytes) -> bytes:
    """构造 DATA_END 帧。

    [0x08][seq(1B)][checksum(2B BE)][remaining_data]
    Checksum 固定在 Byte 2-3（大端序）。
    """
    return struct.pack(">BBH", CMD_DATA_END, seq, checksum) + remaining_data


def build_verify() -> bytes:
    return bytes([CMD_VERIFY, 0x00])


def build_reboot() -> bytes:
    return bytes([CMD_REBOOT, 0x00])


# ── 响应解析器 ──

def parse_header(data: bytes) -> tuple[int, int]:
    """解析帧头，返回 (cmd, seq)。"""
    return data[0], data[1] if len(data) > 1 else 0


def parse_ack(data: bytes) -> tuple[int, int]:
    """解析 ACK 帧：返回 (acked_cmd, status)。

    格式：[0x10][cmd][status][填充]
    """
    if len(data) < 2 or data[0] != CMD_ACK:
        return (-1, -1)
    cmd = data[1]
    status = data[2] if len(data) > 2 else STATUS_OK
    return cmd, status


def parse_nack(data: bytes) -> tuple[int, int]:
    """解析 NACK 帧：返回 (nacked_cmd, error_code)。

    格式：[0x11][cmd][error_code][填充]
    """
    if len(data) < 2 or data[0] != CMD_NACK:
        return (-1, -1)
    cmd = data[1]
    error_code = data[2] if len(data) > 2 else 0
    return cmd, error_code


def is_response_ack(data: bytes, expected_cmd: int) -> bool:
    data_cmd, status = parse_ack(data)
    return data_cmd == expected_cmd and status == STATUS_OK


def is_response_nack(data: bytes) -> bool:
    return len(data) > 0 and data[0] == CMD_NACK


# ── Checksum / CRC ──

def compute_block_checksum(data: bytes) -> int:
    """16-bit 累加和校验（用于每 1KB Block，§4.5）。"""
    return sum(data) & 0xFFFF


def compute_crc32(data: bytes) -> int:
    """标准 CRC32 (用于整包固件，§4.3 METADATA)。"""
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFF


def compute_checksum32(data: bytes) -> int:
    """32-bit 累加和校验（用于 VERIFY 阶段整包固件校验）。"""
    return sum(data) & 0xFFFFFFFF


# ── Block 拆分 ──

def block_chunks(fw_data: bytes, max_frame_size: int) -> list[bytes]:
    """将固件数据按 1KB Block 拆分。

    最后一个 Block 不足 1024 时用零填充到 1024，这样上板的 checksum
    计算（对全部 1024 字节求和）与板端行为一致。
    """
    blocks = []
    for offset in range(0, len(fw_data), BLOCK_SIZE):
        block = fw_data[offset:offset + BLOCK_SIZE]
        block = block.ljust(BLOCK_SIZE, b'\x00')
        blocks.append(block)
    return blocks
