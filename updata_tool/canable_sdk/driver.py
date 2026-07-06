"""CANable 2.5 USB-CAN adapter driver (ElmueSoft protocol).

Main entrypoint: ZDTCanable class.
"""
from __future__ import annotations

import logging
import struct
import threading
import time
from typing import Callable, List, Optional

import usb.core
import usb.util

from .constants import (
    CANABLE_VID, CANABLE_PID, EP_IN, EP_OUT, MAX_PACKET_SIZE,
    HOST_FORMAT_MAGIC, LEGACY_FRAME_SIZE,
    GS_ReqSetHostFormat, GS_ReqSetBitTiming, GS_ReqSetDeviceMode,
    GS_ReqGetCapabilities, GS_ReqGetDeviceVersion, GS_ReqGetTimestamp,
    GS_ReqIdentify, GS_ReqSetBitTimingFD, GS_ReqGetCapabilitiesFD,
    GS_ReqSetTermination, GS_ReqGetTermination,
    ELM_ReqGetBoardInfo, ELM_ReqSetFilter, ELM_ReqGetLastError,
    ELM_ReqSetBusLoadReport,
    GS_DevFlagListenOnly, GS_DevFlagLoopback, GS_DevFlagOneShot,
    GS_DevFlagTimestamp, GS_DevFlagIdentify, GS_DevFlagCAN_FD,
    GS_DevFlagBitTimingFD, GS_DevFlagTermination,
    ELM_DevFlagProtocolElmue, ELM_DevFlagDisableTxEcho,
    logger,
)
from .bitrate import NOMINAL_BITTIMING, DATA_BITTIMING
from .frame import CANFrame
from .protocol import _ElmueProtocol

logger = logging.getLogger("canable_sdk")


class ZDTCanable:
    def __init__(self, vid=None, pid=None, serial=None, port=None, backend=None):
        self.vid    = vid or CANABLE_VID
        self.pid    = pid or CANABLE_PID
        self.serial = serial
        self.dev:    Optional[usb.core.Device] = None
        self.ep_out: Optional[usb.core.Endpoint] = None
        self.ep_in:  Optional[usb.core.Endpoint] = None

        self._lock       = threading.Lock()
        self._running    = False
        self._fd_mode    = False
        self._bitrate:   Optional[int] = None
        self._data_bitrate: Optional[int] = None
        self._capabilities: int = 0
        self._capabilities_fd: int = 0
        self._protocol:  Optional[str] = None
        self._parser:    Optional[_ElmueProtocol] = None
        self._marker_counter: int = 0
        self._has_timestamp: bool = False
        self._listen_only: bool = False
        self._loopback: bool = False
        self._callbacks: List[Callable[[CANFrame], None]] = []
        self._overflow_cb: Optional[Callable[[], None]] = None
        self._last_error_info: Optional[str] = None
        self._tx_blocked_until: float = 0.0

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    @property
    def running(self) -> bool:
        return self._running

    @property
    def fd_mode(self) -> bool:
        return self._fd_mode

    @fd_mode.setter
    def fd_mode(self, value: bool):
        self._fd_mode = value

    # ================================================================
    #  Device enumeration
    # ================================================================
    @staticmethod
    def list_devices() -> List[dict]:
        devs = []
        for d in usb.core.find(find_all=True, idVendor=CANABLE_VID, idProduct=CANABLE_PID):
            try:
                mfg = usb.util.get_string(d, d.iManufacturer) or ""
                prd = usb.util.get_string(d, d.iProduct) or ""
                ser = usb.util.get_string(d, d.iSerialNumber) or ""
            except Exception:
                mfg = prd = ser = ""
            devs.append({
                "backend": "elmue",
                "vid": CANABLE_VID, "pid": CANABLE_PID,
                "manufacturer": mfg, "product": prd, "serial": ser,
                "path": None,
            })
        return devs

    # ================================================================
    #  Connection & disconnection
    # ================================================================
    def open(self):
        kwargs = dict(idVendor=self.vid, idProduct=self.pid)
        if self.serial:
            kwargs.setdefault("serial_number", self.serial)
        self.dev = usb.core.find(**kwargs)
        if self.dev is None:
            raise RuntimeError(f"CANable not found (VID=0x{self.vid:04X}, PID=0x{self.pid:04X})")

        self._setup_usb()
        self._init_device()
        self._detect_protocol()

    def close(self):
        if self._running:
            try:
                self.stop()
            except Exception:
                pass
        try:
            self._ctrl_out(GS_ReqIdentify, data=struct.pack('<I', 0))
        except Exception:
            pass
        if self.dev is not None:
            try:
                usb.util.dispose_resources(self.dev)
            except Exception:
                pass
            self.dev = None
        self.ep_in = self.ep_out = None
        self._running = False

    def _setup_usb(self):
        try:
            if self.dev.is_kernel_driver_active(0):
                self.dev.detach_kernel_driver(0)
        except (usb.core.USBError, NotImplementedError):
            pass

        try:
            self.dev.set_configuration()
        except usb.core.USBError:
            pass
        cfg = self.dev.get_active_configuration()
        intf = cfg[(0, 0)]

        self.ep_in = usb.util.find_descriptor(
            intf, custom_match=lambda e: e.bEndpointAddress == EP_IN)
        self.ep_out = usb.util.find_descriptor(
            intf, custom_match=lambda e: e.bEndpointAddress == EP_OUT)

        if self.ep_out is None:
            self.ep_out = usb.util.find_descriptor(
                intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
                                               == usb.util.ENDPOINT_OUT)
        if self.ep_in is None:
            self.ep_in = usb.util.find_descriptor(
                intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
                                               == usb.util.ENDPOINT_IN)

        if self.ep_out is None or self.ep_in is None:
            raise RuntimeError(f"USB endpoints not found (need EP_IN=0x81, EP_OUT=0x02)")

    def _init_device(self):
        self._reset_device()

        self._ctrl_out_checked(GS_ReqSetHostFormat,
                               data=struct.pack('<I', HOST_FORMAT_MAGIC))

        caps = self._ctrl_in(GS_ReqGetCapabilities, length=44)
        if caps and len(caps) >= 4:
            self._capabilities = struct.unpack_from('<I', bytes(caps), 0)[0]
            logger.info("capabilities: 0x%X", self._capabilities)

        ver = self._ctrl_in(GS_ReqGetDeviceVersion, length=16)
        if ver and len(ver) >= 12:
            sw = struct.unpack_from('<I', bytes(ver), 4)[0]
            hw = struct.unpack_from('<I', bytes(ver), 8)[0]
            logger.info("firmware: 0x%08X, hardware: 0x%08X", sw, hw)

    def _reset_device(self):
        flags = ELM_DevFlagProtocolElmue
        try:
            self._ctrl_out(GS_ReqSetDeviceMode,
                           data=struct.pack('<II', 0, flags))
            time.sleep(0.05)
        except usb.core.USBError:
            logger.debug("RESET failed (device may already be closed)")

    def _detect_protocol(self):
        if self._capabilities & ELM_DevFlagProtocolElmue:
            self._protocol = "elmue"
            self._parser = _ElmueProtocol()
            logger.info("using ElmueSoft variable-length protocol")
        else:
            self._protocol = "legacy"
            self._parser = None
            logger.info("using Legacy fixed-length protocol (80 bytes)")

    # ================================================================
    #  USB control transfers
    # ================================================================
    def _ctrl_out(self, req, value=0, index=0, data=None, timeout=1000):
        bmRequestType = (usb.util.CTRL_OUT
                         | usb.util.CTRL_TYPE_VENDOR
                         | usb.util.CTRL_RECIPIENT_INTERFACE)
        if data is None:
            data = []
        return self.dev.ctrl_transfer(bmRequestType, req, value, index, data, timeout)

    def _ctrl_in(self, req, value=0, index=0, length=64, timeout=1000):
        bmRequestType = (usb.util.CTRL_IN
                         | usb.util.CTRL_TYPE_VENDOR
                         | usb.util.CTRL_RECIPIENT_INTERFACE)
        return self.dev.ctrl_transfer(bmRequestType, req, value, index, length, timeout)

    def _ctrl_out_checked(self, req, value=0, index=0, data=None, timeout=1000):
        self._ctrl_out(req, value, index, data, timeout)
        self._check_last_error()

    def _check_last_error(self):
        try:
            result = self._ctrl_in(ELM_ReqGetLastError, length=1)
            if result and len(result) >= 1:
                feedback = result[0]
                if feedback != 0 and feedback != 2:
                    logger.warning("control request failed: eFeedback=%d", feedback)
        except usb.core.USBError:
            pass

    # ================================================================
    #  CAN controller lifecycle
    # ================================================================
    def start(self, loopback: bool = False):
        flags = 0
        if self._protocol == "elmue":
            flags |= ELM_DevFlagProtocolElmue

        if self._listen_only:
            flags |= GS_DevFlagListenOnly

        if loopback or self._loopback:
            flags |= GS_DevFlagLoopback

        if self._fd_mode:
            flags |= GS_DevFlagCAN_FD

        mode = 1  # GS_ModeStart
        payload = struct.pack('<II', mode, flags)
        self._ctrl_out_checked(GS_ReqSetDeviceMode, data=payload)

        self._has_timestamp = bool(flags & GS_DevFlagTimestamp)
        if self._parser and isinstance(self._parser, _ElmueProtocol):
            self._parser.set_timestamp_mode(self._has_timestamp)

        self._running = True
        time.sleep(0.05)  # allow transceiver/isolator to power up
        logger.info("CAN started (protocol=%s, FD=%s, loopback=%s, flags=0x%X, bitrate=%s, data_bitrate=%s)",
                    self._protocol, self._fd_mode, bool(flags & GS_DevFlagLoopback),
                    flags, self._bitrate, self._data_bitrate)

    def stop(self):
        flags = 0
        if self._protocol == "elmue":
            flags |= ELM_DevFlagProtocolElmue
        try:
            self._ctrl_out_checked(GS_ReqSetDeviceMode,
                                   data=struct.pack('<II', 0, flags))
        except usb.core.USBError as e:
            logger.warning("stop USB error: %s", e)
        self._running = False
        logger.info("CAN stopped")

    def recover(self):
        flags = ELM_DevFlagProtocolElmue if self._protocol == "elmue" else 0
        self._ctrl_out(GS_ReqSetDeviceMode, data=struct.pack('<II', 0, flags))
        time.sleep(0.05)

        if self._parser is not None:
            self._parser.clear()

        try:
            for _ in range(16):
                self.ep_in.read(256, timeout=10)
        except usb.core.USBError:
            pass

        if self._bitrate:
            self.set_bitrate(self._bitrate)
        if self._fd_mode and self._data_bitrate:
            self.set_data_bitrate(self._data_bitrate)

        self.start(loopback=self._loopback)
        self._tx_blocked_until = time.time() + 0.3
        logger.info("CAN recovered")

    # ================================================================
    #  Bit timing configuration
    # ================================================================
    def set_bitrate(self, bitrate: int):
        if bitrate not in NOMINAL_BITTIMING:
            brp, seg1, seg2, sjw = self._calc_bitrate_params(bitrate)
        else:
            brp, seg1, seg2, sjw = NOMINAL_BITTIMING[bitrate]

        prop = 0
        payload = struct.pack('<IIIII', prop, seg1, seg2, sjw, brp)
        self._ctrl_out_checked(GS_ReqSetBitTiming, data=payload)
        self._bitrate = bitrate
        logger.info("nominal bitrate set: %d bps (brp=%d seg1=%d seg2=%d sjw=%d)",
                    bitrate, brp, seg1, seg2, sjw)

    def set_data_bitrate(self, data_bitrate: int):
        if data_bitrate not in DATA_BITTIMING:
            brp, seg1, seg2, sjw = self._calc_bitrate_params(data_bitrate)
        else:
            brp, seg1, seg2, sjw = DATA_BITTIMING[data_bitrate]

        prop = 0
        payload = struct.pack('<IIIII', prop, seg1, seg2, sjw, brp)
        self._ctrl_out_checked(GS_ReqSetBitTimingFD, data=payload)
        self._data_bitrate = data_bitrate
        logger.info("data bitrate set: %d bps", data_bitrate)

    def _calc_bitrate_params(self, bitrate: int):
        clock = 160_000_000
        best_err = float('inf')
        best = None
        for seg1 in range(1, 33):
            for seg2 in range(1, min(seg1 + 1, 17)):
                total = 1 + seg1 + seg2
                for brp in range(1, 513):
                    calc = clock / brp / total
                    err = abs(calc - bitrate) / bitrate
                    if err < best_err:
                        best_err = err
                        best = (brp, seg1, seg2, min(seg2, 4))
                    if err < 0.001:
                        return best
        if best_err > 0.05:
            raise ValueError(f"cannot compute bit timing for {bitrate} bps (error {best_err:.1%})")
        return best

    # ================================================================
    #  Send / Receive
    # ================================================================
    def send(self, frame: CANFrame, timeout: int = 1000):
        if not self._running:
            raise RuntimeError("CAN not started")

        now = time.time()
        if now < self._tx_blocked_until:
            raise RuntimeError("CAN recovering, please wait")

        if self._protocol == "elmue":
            self._marker_counter = (self._marker_counter + 1) & 0xFF
            if self._marker_counter == 0:
                self._marker_counter = 1
            marker = self._marker_counter
            raw = frame.to_elmue_bytes(marker=marker)
            if self._parser and isinstance(self._parser, _ElmueProtocol):
                self._parser.store_tx_frame(marker, frame)
        else:
            raw = frame.to_legacy_bytes()

        try:
            with self._lock:
                self.ep_out.write(raw, timeout=timeout)
                if len(raw) > 0 and len(raw) % MAX_PACKET_SIZE == 0:
                    self.ep_out.write(b'', timeout=timeout)
        except usb.core.USBError as e:
            if getattr(e, 'errno', None) in (32, 232) or 'pipe' in str(e).lower():
                logger.warning("USB pipe error, clearing STALL")
                try:
                    usb.util.clear_stall(self.ep_out)
                except Exception:
                    pass
                self.recover()
                self._tx_blocked_until = time.time() + 0.5
            raise

    def send_periodic(self, frame: CANFrame, interval_s: float = 0.01, count: int = 0):
        sent = 0
        while count == 0 or sent < count:
            self.send(frame)
            sent += 1
            if count == 0 or sent < count:
                time.sleep(interval_s)

    def receive(self, timeout: float = 1.0) -> Optional[CANFrame]:
        if not self._running:
            raise RuntimeError("CAN not started")

        if self._protocol == "elmue":
            return self._recv_elmue(timeout)
        else:
            return self._recv_legacy(timeout)

    def _recv_elmue(self, timeout: float = 1.0) -> Optional[CANFrame]:
        if self._parser is not None:
            frames = self._parser.flush_frames()
            if frames:
                if len(frames) > 1:
                    self._parser._pending_frames = frames[1:]
                return frames[0]

        try:
            data = self.ep_in.read(256, timeout=int(timeout * 1000))
        except usb.core.USBError as e:
            errno = getattr(e, "errno", None)
            if errno in (110, 10060) or "timeout" in str(e).lower():
                return None
            if errno in (75, 121) or "overflow" in str(e).lower():
                logger.debug("USB overflow, flushing endpoint buffer")
                try:
                    self.ep_in.read(256, timeout=10)
                except Exception:
                    pass
                return None
            raise

        if not data:
            return None

        raw = bytes(data)
        logger.debug("USB IN %3d hex: %s", len(raw), raw[:32].hex())
        frames = self._parser.feed(raw)
        if frames:
            logger.debug("parsed %d frames", len(frames))
        else:
            logger.debug("no frame from %d bytes (string/busload)", len(raw))

        if frames:
            if len(frames) > 1:
                self._parser._pending_frames = frames[1:]
            return frames[0]
        return None

    def _recv_legacy(self, timeout: float = 1.0) -> Optional[CANFrame]:
        try:
            data = self.ep_in.read(LEGACY_FRAME_SIZE, timeout=int(timeout * 1000))
        except usb.core.USBError as e:
            errno = getattr(e, "errno", None)
            if errno in (110, 10060) or "timeout" in str(e).lower():
                return None
            if errno in (75, 121) or "overflow" in str(e).lower():
                try:
                    self.ep_in.read(LEGACY_FRAME_SIZE, timeout=10)
                except Exception:
                    pass
                return None
            raise

        if not data:
            return None

        return CANFrame.from_legacy_bytes(bytes(data))

    # ================================================================
    #  Feature queries & settings
    # ================================================================
    def check_fd_support(self) -> bool:
        try:
            caps_fd = self._ctrl_in(GS_ReqGetCapabilitiesFD, length=72)
            if caps_fd and len(caps_fd) >= 4:
                self._capabilities_fd = struct.unpack_from('<I', bytes(caps_fd), 0)[0]
        except usb.core.USBError:
            pass

        fd_supported = bool((self._capabilities | self._capabilities_fd) & GS_DevFlagCAN_FD)
        fd_timing = bool((self._capabilities | self._capabilities_fd) & GS_DevFlagBitTimingFD)
        supported = fd_supported and fd_timing
        logger.info("FD support: %s (caps=0x%X, caps_fd=0x%X)",
                    supported, self._capabilities, self._capabilities_fd)
        return supported

    def get_version(self) -> Optional[str]:
        try:
            ver = self._ctrl_in(GS_ReqGetDeviceVersion, length=16)
            if ver and len(ver) >= 12:
                sw = struct.unpack_from('<I', bytes(ver), 4)[0]
                return f"0x{sw:08X}"
        except usb.core.USBError:
            pass
        return None

    def read_error_register(self) -> Optional[str]:
        return self._last_error_info

    def identify(self, duration_ms: int = 1000):
        try:
            self._ctrl_out(GS_ReqIdentify, data=struct.pack('<I', 1))
            time.sleep(duration_ms / 1000.0)
            self._ctrl_out(GS_ReqIdentify, data=struct.pack('<I', 0))
            time.sleep(0.02)
        except usb.core.USBError:
            pass

    def set_silent(self, enable: bool) -> bool:
        was_running = self._running
        if was_running:
            self.stop()
        self._listen_only = enable
        if was_running:
            self.start()
        return True

    # ================================================================
    #  Extended features
    # ================================================================
    def set_filter(self, operation: int = 0, can_id: int = 0, mask: int = 0):
        payload = struct.pack('<BIIII', operation, can_id, mask, 0, 0)
        self._ctrl_out_checked(ELM_ReqSetFilter, data=payload)
        logger.info("filter set: op=%d id=0x%X mask=0x%X", operation, can_id, mask)

    def get_termination(self) -> Optional[bool]:
        try:
            result = self._ctrl_in(GS_ReqGetTermination, length=4)
            if result and len(result) >= 4:
                return bool(struct.unpack_from('<I', bytes(result), 0)[0])
        except usb.core.USBError:
            pass
        return None

    def set_termination(self, enabled: bool):
        payload = struct.pack('<I', 1 if enabled else 0)
        self._ctrl_out_checked(GS_ReqSetTermination, data=payload)
        logger.info("termination: %s", "ON" if enabled else "OFF")

    def set_bus_load_report(self, interval: int = 0):
        payload = struct.pack('<B', interval)
        self._ctrl_out_checked(ELM_ReqSetBusLoadReport, data=payload)

    # ================================================================
    #  Callbacks & listening
    # ================================================================
    def on_receive(self, callback: Callable[[CANFrame], None]):
        self._callbacks.append(callback)

    def on_overflow(self, callback: Callable[[], None]):
        self._overflow_cb = callback

    def start_listening(self):
        self._running = True
        self._recv_thr = threading.Thread(target=self._listen_loop, daemon=True)
        self._recv_thr.start()

    def _listen_loop(self):
        while self._running:
            try:
                frame = self.receive(timeout=0.1)
                if frame is not None:
                    for cb in self._callbacks:
                        try:
                            cb(frame)
                        except Exception:
                            pass
            except Exception as e:
                if self._running:
                    logger.warning("receive error: %s", e)
                time.sleep(0.01)
