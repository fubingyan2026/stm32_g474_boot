"""CAN frame data class and DLC helpers.

CANable 2.5 — ElmueSoft protocol frame serialization/deserialization.
"""
from __future__ import annotations

import struct
import time
from dataclasses import dataclass
from typing import Optional

from .constants import (
    CAN_FD_DLC_MAP, DLC_BOUNDARIES,
    MSG_TxFrame,
    CAN_ID_29Bit, CAN_ID_RTR, CAN_MASK_11, CAN_MASK_29,
    FRM_FDF, FRM_BRS, FRM_ESI,
    ERID_Bus_is_off, ERID_No_ACK_received, ERID_CRC_Error,
    ERID_Tx_Timeout, ERID_Arbitration_lost,
    BUS_StatusOff, BUS_StatusPassive, BUS_StatusWarning, ER1_Bus_is_back_active,
    APP_CanTxOverflow, APP_CanTxTimeout, APP_CanRxFail, APP_CanTxFail, APP_UsbInOverflow,
)


def _pad_to_dlc(data_len: int) -> int:
    if data_len <= 8:
        return data_len
    for b in DLC_BOUNDARIES:
        if data_len <= b:
            return b
    return 64


def _data_len_to_dlc(data_len: int) -> int:
    if data_len <= 8:
        return data_len
    for dlc, length in sorted(CAN_FD_DLC_MAP.items()):
        if dlc > 8 and length >= data_len:
            return dlc
    return 15


def _dlc_to_data_len(dlc: int) -> int:
    return CAN_FD_DLC_MAP.get(dlc, 64)


@dataclass
class CANFrame:
    can_id:      int
    data:        bytes = b""
    extended:    bool  = False
    rtr:         bool  = False
    fd:          bool  = False
    brs:         bool  = False
    esi:         bool  = False
    timestamp:   float = 0.0
    echo_id:     int   = 0
    is_tx:       bool  = False
    _error_info: str   = ""

    def __post_init__(self):
        if not isinstance(self.data, bytes):
            self.data = bytes(self.data)

    @property
    def is_error(self) -> bool:
        return bool(self._error_info)

    @property
    def dlc(self) -> int:
        if self.fd:
            return _data_len_to_dlc(len(self.data))
        return min(len(self.data), 8)

    @staticmethod
    def dlc_to_len(dlc: int) -> int:
        return _dlc_to_data_len(dlc)

    def __str__(self) -> str:
        id_fmt = f"{self.can_id:08X}" if self.extended else f"{self.can_id:03X}"
        kind = "EFF" if self.extended else "SFF"
        parts = [kind]
        if self.fd:
            parts.append("FD")
        if self.brs:
            parts.append("BRS")
        if self.esi:
            parts.append("ESI")
        if self.rtr:
            parts.append("RTR")
        data_hex = " ".join(f"{b:02X}" for b in self.data)
        return f"[{id_fmt} {' '.join(parts)}] {data_hex}"

    def __repr__(self) -> str:
        return f"CANFrame({self})"

    # ----- ElmueSoft TX packing ----- #
    def to_elmue_bytes(self, marker: int = 0) -> bytes:
        can_id_raw = self.can_id
        if self.extended:
            can_id_raw |= CAN_ID_29Bit
        if self.rtr:
            can_id_raw |= CAN_ID_RTR

        flags = 0
        if self.fd:
            flags |= FRM_FDF
        if self.brs:
            flags |= FRM_BRS
        if self.esi:
            flags |= FRM_ESI

        raw_data = bytes(self.data)
        if self.fd:
            padded_len = _pad_to_dlc(len(raw_data))
            raw_data = raw_data.ljust(padded_len, b'\x00')
        else:
            raw_data = raw_data.ljust(min(len(raw_data), 8), b'\x00')[:8] if raw_data else b''

        size = 8 + len(raw_data)
        header = struct.pack('<BB', size, MSG_TxFrame)
        body = struct.pack('<BIB', flags, can_id_raw, marker)
        return header + body + raw_data

    # ----- ElmueSoft RX parsing ----- #
    @classmethod
    def from_elmue_rx(cls, raw: bytes, has_timestamp: bool) -> "CANFrame":
        offset = 2
        flags = raw[offset]
        can_id_raw = struct.unpack_from('<I', raw, offset + 1)[0]
        offset += 5

        ts_us = 0
        if has_timestamp:
            ts_us = struct.unpack_from('<I', raw, offset)[0]
            offset += 4

        data = raw[offset:]

        extended = bool(can_id_raw & CAN_ID_29Bit)
        rtr = bool(can_id_raw & CAN_ID_RTR)
        can_id = can_id_raw & CAN_MASK_29 if extended else can_id_raw & CAN_MASK_11
        is_fd = bool(flags & FRM_FDF)
        ts = ts_us / 1_000_000.0 if ts_us else time.time()

        return cls(
            can_id=can_id, data=bytes(data), extended=extended, rtr=rtr,
            fd=is_fd, brs=bool(flags & FRM_BRS), esi=bool(flags & FRM_ESI),
            timestamp=ts, is_tx=False,
        )

    # ----- ElmueSoft TX echo parsing ----- #
    @classmethod
    def from_elmue_echo(cls, raw: bytes, has_timestamp: bool,
                        tx_frames: dict = None) -> "CANFrame":
        marker = raw[2]
        ts_us = 0
        if has_timestamp and len(raw) >= 7:
            ts_us = struct.unpack_from('<I', raw, 3)[0]
        ts = ts_us / 1_000_000.0 if ts_us else time.time()

        if tx_frames and marker in tx_frames:
            orig = tx_frames[marker]
            return cls(
                can_id=orig.can_id, data=orig.data, extended=orig.extended,
                rtr=orig.rtr, fd=orig.fd, brs=orig.brs, esi=orig.esi,
                timestamp=ts, echo_id=marker, is_tx=True,
            )

        return cls(can_id=0, data=b"", timestamp=ts, echo_id=marker, is_tx=True)

    # ----- Legacy parsing ----- #
    @classmethod
    def from_legacy_bytes(cls, raw: bytes) -> Optional["CANFrame"]:
        if len(raw) < 12:
            return None

        echo_id, can_id_full = struct.unpack('<II', raw[:8])
        dlc = raw[8]
        flags = raw[10]

        if dlc > 15:
            return None

        is_fd = bool(flags & FRM_FDF)
        actual_len = cls.dlc_to_len(dlc) if is_fd else min(dlc, 8)
        data = raw[12:12 + actual_len]

        ts = time.time()
        if is_fd and len(raw) >= 80:
            ts_us = struct.unpack_from('<I', raw, 76)[0]
            if ts_us:
                ts = ts_us / 1_000_000.0
        elif not is_fd and len(raw) >= 24:
            ts_us = struct.unpack_from('<I', raw, 20)[0]
            if ts_us:
                ts = ts_us / 1_000_000.0

        extended = bool(can_id_full & CAN_ID_29Bit)
        rtr = bool(can_id_full & CAN_ID_RTR)
        can_id = can_id_full & CAN_MASK_29 if extended else can_id_full & CAN_MASK_11

        is_error = bool(can_id_full & CAN_ID_Error)
        if is_error:
            parts = []
            if can_id_full & ERID_Bus_is_off:
                parts.append("BUS-OFF")
            if can_id_full & ERID_No_ACK_received:
                parts.append("NO-ACK")
            if can_id_full & ERID_CRC_Error:
                parts.append("CRC-ERR")
            if not parts:
                parts.append(f"ERR-0x{can_id_full & ~CAN_ID_Error:02X}")
            return cls(
                can_id=0, data=bytes([can_id_full & 0xFF]),
                timestamp=time.time(), is_tx=False,
                _error_info=" ".join(parts),
            )

        is_tx = (echo_id != 0xFFFFFFFF)
        return cls(
            can_id=can_id, data=bytes(data), extended=extended, rtr=rtr,
            fd=is_fd, brs=bool(flags & FRM_BRS), esi=bool(flags & FRM_ESI),
            timestamp=ts, echo_id=echo_id if is_tx else 0, is_tx=is_tx,
        )

    # ----- Legacy TX packing ----- #
    def to_legacy_bytes(self) -> bytes:
        can_id_raw = self.can_id
        if self.extended:
            can_id_raw |= CAN_ID_29Bit
        if self.rtr:
            can_id_raw |= CAN_ID_RTR

        flags = 0
        if self.fd:
            flags |= FRM_FDF
        if self.brs:
            flags |= FRM_BRS
        if self.esi:
            flags |= FRM_ESI

        echo_id = self.echo_id if self.echo_id else 1
        dlc = self.dlc

        if self.fd:
            data_padded = bytes(self.data).ljust(64, b'\x00')
            return struct.pack('<IIBBBB64sI',
                               echo_id, can_id_raw, dlc, 0,
                               flags, 0, data_padded, 0)
        else:
            data_padded = bytes(self.data).ljust(8, b'\x00')[:8]
            return struct.pack('<IIBBBB8s',
                               echo_id, can_id_raw, dlc, 0,
                               flags, 0, data_padded)
