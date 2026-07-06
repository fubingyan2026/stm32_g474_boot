"""ElmueSoft variable-length protocol stream parser."""

import logging
import struct
import time
from typing import Optional

from .constants import (
    MAX_ELMUE_MSG_SIZE, VALID_MSG_TYPES,
    MSG_RxFrame, MSG_TxEcho, MSG_TxFrame, MSG_Error, MSG_String, MSG_Busload,
    CAN_ID_29Bit, CAN_ID_RTR, CAN_MASK_11, CAN_MASK_29,
    FRM_FDF, FRM_BRS, FRM_ESI,
    ERID_Bus_is_off, ERID_No_ACK_received, ERID_CRC_Error,
    ERID_Tx_Timeout, ERID_Arbitration_lost,
    BUS_StatusOff, BUS_StatusPassive, BUS_StatusWarning, ER1_Bus_is_back_active,
    APP_CanTxOverflow, APP_CanTxTimeout, APP_CanRxFail, APP_CanTxFail, APP_UsbInOverflow,
)
from .frame import CANFrame

logger = logging.getLogger("canable_sdk.protocol")

MAX_BUFFER_SIZE = 2048


class _ElmueProtocol:
    def __init__(self):
        self._buffer = bytearray()
        self._has_timestamp = False
        self._tx_frames: dict = {}
        self._pending_frames: list = []

    def set_timestamp_mode(self, enabled: bool):
        self._has_timestamp = enabled

    def store_tx_frame(self, marker: int, frame: CANFrame):
        self._tx_frames[marker] = frame

    def clear(self):
        self._buffer.clear()
        self._pending_frames.clear()
        self._tx_frames.clear()

    def feed(self, raw: bytes) -> list:
        self._buffer.extend(raw)
        if len(self._buffer) > MAX_BUFFER_SIZE:
            logger.warning("buffer overflow, discarding %d bytes", len(self._buffer))
            self._buffer.clear()
            return []

        frames = []
        while len(self._buffer) >= 2:
            size = self._buffer[0]
            msg_type = self._buffer[1]

            if size < 2 or size > MAX_ELMUE_MSG_SIZE or msg_type not in VALID_MSG_TYPES:
                self._buffer.pop(0)
                continue

            if len(self._buffer) < size:
                break

            msg = bytes(self._buffer[:size])
            del self._buffer[:size]

            frame = self._parse_message(msg, msg_type)
            if frame is not None:
                frames.append(frame)

        return frames

    def flush_frames(self) -> list:
        frames = self._pending_frames
        self._pending_frames = []
        return frames

    def _parse_message(self, msg: bytes, msg_type: int) -> Optional[CANFrame]:
        logger.debug("parse: type=%d size=%d", msg_type, len(msg))
        if msg_type == MSG_RxFrame:
            return CANFrame.from_elmue_rx(msg, self._has_timestamp)
        elif msg_type == MSG_TxEcho:
            return CANFrame.from_elmue_echo(msg, self._has_timestamp, self._tx_frames)
        elif msg_type == MSG_TxFrame:
            return self._parse_tx_frame(msg)
        elif msg_type == MSG_Error:
            return self._parse_error(msg)
        elif msg_type == MSG_String:
            self._parse_string(msg)
            return None
        elif msg_type == MSG_Busload:
            self._parse_busload(msg)
            return None
        else:
            logger.warning("unknown msg type: %d (size=%d)", msg_type, len(msg))
            return None

    def _parse_tx_frame(self, msg: bytes) -> Optional[CANFrame]:
        if len(msg) < 8:
            return None
        flags = msg[2]
        can_id_raw = struct.unpack_from('<I', msg, 3)[0]
        data = msg[8:]

        extended = bool(can_id_raw & CAN_ID_29Bit)
        rtr = bool(can_id_raw & CAN_ID_RTR)
        can_id = can_id_raw & CAN_MASK_29 if extended else can_id_raw & CAN_MASK_11

        return CANFrame(
            can_id=can_id, data=bytes(data), extended=extended, rtr=rtr,
            fd=bool(flags & FRM_FDF), brs=bool(flags & FRM_BRS),
            esi=bool(flags & FRM_ESI), timestamp=time.time(), is_tx=True,
        )

    def _parse_error(self, msg: bytes) -> CANFrame:
        if len(msg) < 6:
            return CANFrame(can_id=0, _error_info="ERR-SHORT")

        err_id = struct.unpack_from('<I', msg, 2)[0]
        err_data = msg[6:14] if len(msg) >= 14 else b'\x00' * 8

        parts = []

        if err_id & ERID_Bus_is_off:
            parts.append("BUS-OFF")
        if err_id & ERID_No_ACK_received:
            parts.append("NO-ACK")
        if err_id & ERID_CRC_Error:
            parts.append("CRC-ERR")
        if err_id & ERID_Tx_Timeout:
            parts.append("TX-TIMEOUT")
        if err_id & ERID_Arbitration_lost:
            parts.append("ARB-LOST")

        byte1 = err_data[1] if len(err_data) > 1 else 0
        bus_status = byte1 & 0x30
        if bus_status == BUS_StatusOff:
            parts.append("BUS-OFF")
        elif bus_status == BUS_StatusPassive:
            parts.append("ERROR-PASSIVE")
        elif bus_status == BUS_StatusWarning:
            parts.append("ERROR-WARNING")
        elif byte1 & ER1_Bus_is_back_active:
            parts.append("BACK-ACTIVE")

        app_flags = err_data[5] if len(err_data) > 5 else 0
        if app_flags & APP_CanTxOverflow:
            parts.append("TX-OVERFLOW")
        if app_flags & APP_CanTxTimeout:
            parts.append("TX-TIMEOUT")
        if app_flags & APP_CanRxFail:
            parts.append("RX-FAIL")
        if app_flags & APP_CanTxFail:
            parts.append("TX-FAIL")
        if app_flags & APP_UsbInOverflow:
            parts.append("USB-OVERFLOW")

        tx_err = err_data[6] if len(err_data) > 6 else 0
        rx_err = err_data[7] if len(err_data) > 7 else 0
        if tx_err or rx_err:
            parts.append(f"TEC={tx_err} REC={rx_err}")

        if not parts:
            parts.append(f"ERR-0x{err_id:02X}")

        return CANFrame(
            can_id=0, data=bytes([err_id & 0xFF]),
            timestamp=time.time(), is_tx=False,
            _error_info=" ".join(parts),
        )

    def _parse_string(self, msg: bytes):
        text = msg[2:].decode('ascii', errors='replace')
        text = text.replace('\n', ' ').replace('\r', ' ')
        logger.debug("device: %s", text.strip())

    def _parse_busload(self, msg: bytes):
        if len(msg) >= 3:
            load = msg[2]
            logger.debug("busload: %d%%", load)
