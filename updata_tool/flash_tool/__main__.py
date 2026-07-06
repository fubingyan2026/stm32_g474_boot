"""CANable 固件升级上位机 — 入口。

用法：
    python -m updata_tool.flash_tool
    python updata_tool/flash_tool/__main__.py
"""
from __future__ import annotations

import sys
import os

# 确保能从 updata_tool/ 找到 canable_sdk
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication

from .main_window import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setApplicationName(MainWindow.APP_NAME)
    app.setOrganizationName(MainWindow.ORG_NAME)

    win = MainWindow()
    win.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
