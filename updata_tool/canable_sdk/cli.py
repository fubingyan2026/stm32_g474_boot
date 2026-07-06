"""CLI entrypoints for direct device testing.
"""
from __future__ import annotations

import logging
import sys
import time

from .constants import logger
from .frame import CANFrame
from .driver import ZDTCanable


def _cli():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")

    devs = ZDTCanable.list_devices()
    if not devs:
        print("No CANable devices found")
        return

    print(f"Found {len(devs)} device(s):")
    for i, d in enumerate(devs):
        print(f"  [{i}] {d['manufacturer']} {d['product']} S/N: {d['serial']}")

    fd_mode = "--fd" in sys.argv

    with ZDTCanable() as bus:
        bus.fd_mode = fd_mode
        bus.set_bitrate(500_000)
        if fd_mode:
            bus.set_data_bitrate(2_000_000)
        bus.start()

        print(f"Listening (CAN FD={'ON' if fd_mode else 'OFF'}, Ctrl+C to exit)")
        try:
            while True:
                frame = bus.receive(timeout=1.0)
                if frame is not None:
                    print(frame)
        except KeyboardInterrupt:
            pass


def _cli_fd_demo():
    logging.basicConfig(level=logging.INFO)

    with ZDTCanable() as bus:
        bus.fd_mode = True
        bus.set_bitrate(500_000)
        bus.set_data_bitrate(2_000_000)
        bus.start()

        print("CAN FD receive demo (Ctrl+C to exit)")
        try:
            while True:
                frame = bus.receive(timeout=1.0)
                if frame is not None:
                    print(frame)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    _cli()
