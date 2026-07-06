"""cangui 命令行入口。

用法：
    python -m cangui
    python cangui.py
"""
import sys
import os

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, os.path.dirname(_HERE))

from PySide6.QtWidgets import QApplication
from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon

from cangui.main_window import MainWindow
from cangui.style import get_qss

LOGO_PATH = os.path.join(_HERE, "logo.jpg")


def main():
    QApplication.setHighDpiScaleFactorRoundingPolicy(
        Qt.HighDpiScaleFactorRoundingPolicy.PassThrough
    )
    app = QApplication(sys.argv)
    app.setOrganizationName("canable")
    app.setApplicationName("CANable2.5")
    app.setWindowIcon(QIcon(LOGO_PATH))
    app.setStyleSheet(get_qss())

    win = MainWindow()
    win.setWindowIcon(QIcon(LOGO_PATH))
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
