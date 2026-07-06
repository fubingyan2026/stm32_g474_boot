"""CANable 2.5 Python SDK — USB-CAN driver for ElmueSoft firmware.

Quick start:
    from canable_sdk import ZDTCanable, CANFrame

    with ZDTCanable() as bus:
        bus.set_bitrate(500_000)
        bus.start()
        frame = bus.receive(timeout=1.0)
"""
from __future__ import annotations

import os
import sys as _sys

# On Windows, add the project root (containing libusb-1.0.dll)
# to the DLL search path so pyusb can find the backend.
if _sys.platform == "win32":
    _dll_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.add_dll_directory(_dll_dir)

from .driver import ZDTCanable  # noqa: E402
from .frame import CANFrame  # noqa: E402
from .constants import logger, CANABLE_VID, CANABLE_PID, FRM_FDF, FRM_BRS, FRM_ESI  # noqa: E402

__version__ = "0.1.0"

__all__ = [
    "ZDTCanable",
    "CANFrame",
    "logger",
    "CANABLE_VID", "CANABLE_PID",
]
