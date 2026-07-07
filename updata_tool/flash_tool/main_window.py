"""CANable 固件升级上位机 — 主窗口。"""
from __future__ import annotations

import os
import sys

from PySide6.QtCore import Qt, QThread
from PySide6.QtGui import QAction
from PySide6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QSplitter,
    QMenuBar, QStatusBar, QLabel, QMessageBox,
)

from canable_sdk import ZDTCanable

from .widgets.device_panel import DevicePanel, BITRATES
from .widgets.firmware_panel import FirmwarePanel
from .worker import FlashWorker, FlashConfig


class MainWindow(QMainWindow):
    APP_NAME = "STM32 CAN Bootloader Flash Tool"
    ORG_NAME = "stm32-g474-boot"

    def __init__(self):
        super().__init__()
        self.setWindowTitle(self.APP_NAME)
        self.resize(1000, 700)

        self._worker = None
        self._worker_thread = None
        self._build_menu()
        self._build_ui()
        self._wire_signals()

        # 初始扫描设备
        self._scan_devices()

    def _build_menu(self):
        mb = self.menuBar()

        file_menu = mb.addMenu("文件")
        act_quit = QAction("退出", self)
        act_quit.setShortcut("Ctrl+Q")
        act_quit.triggered.connect(self.close)
        file_menu.addAction(act_quit)

        help_menu = mb.addMenu("帮助")
        act_about = QAction("关于", self)
        act_about.triggered.connect(self._show_about)
        help_menu.addAction(act_about)

    def _build_ui(self):
        # 左侧：设备面板
        self.device_panel = DevicePanel(self)

        # 右侧：固件面板
        self.fw_panel = FirmwarePanel(self)

        # 主分割
        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(self.device_panel)
        splitter.addWidget(self.fw_panel)
        splitter.setStretchFactor(0, 0)  # 左侧固定宽度
        splitter.setStretchFactor(1, 1)  # 右侧弹性
        splitter.setSizes([280, 720])
        self.setCentralWidget(splitter)

        # 状态栏
        sb = self.statusBar()
        self.status_label = QLabel("就绪")
        sb.addPermanentWidget(self.status_label)

    def _wire_signals(self):
        # 设备连接
        self.device_panel.connected.connect(self._on_device_connect)
        self.device_panel.disconnected.connect(self._on_device_disconnect)
        # 升级控制
        self.fw_panel.start_requested.connect(self._on_start_flash)
        self.fw_panel.stop_requested.connect(self._on_stop_flash)

    def _scan_devices(self):
        self.device_panel._scan_devices()

    def _on_device_connect(self):
        dev = self.device_panel.get_selected_device()
        if dev is None:
            QMessageBox.warning(self, "警告", "请先扫描并选择 CAN 设备")
            self.device_panel.set_disconnected()
            return
        try:
            # 关闭已有的连接（例如上次升级成功后 worker 传回的 bus）
            if hasattr(self, '_test_bus') and self._test_bus is not None:
                try:
                    self._test_bus.close()
                except Exception:
                    pass
                self._test_bus = None

            cfg = self.device_panel.get_config_dict()

            # 测试连接
            bus = ZDTCanable()
            bus.open()

            if cfg["fd_mode"]:
                bus.fd_mode = True
                bus.set_data_bitrate(cfg["data_bitrate"])
                bus.set_bitrate(cfg["bitrate"])
            else:
                bus.set_bitrate(cfg["bitrate"])
            bus.stop()

            self._test_bus = bus
            self.device_panel.set_connected()
            self.status_label.setText(f"已连接: {dev.get('product','CANable')}")
        except Exception as e:
            self.device_panel.set_disconnected()
            QMessageBox.critical(self, "连接失败", str(e))

    def _on_device_disconnect(self):
        if hasattr(self, '_test_bus') and self._test_bus is not None:
            try:
                self._test_bus.close()
            except Exception:
                pass
            self._test_bus = None
        self.device_panel.set_disconnected()
        self.status_label.setText("未连接")

    def _on_start_flash(self):
        if not self.device_panel._connected:
            QMessageBox.warning(self, "警告", "请先连接 CAN 设备")
            return

        fw_path = self.fw_panel.get_fw_path()
        if not fw_path or not os.path.isfile(fw_path):
            QMessageBox.warning(self, "警告", "请选择有效的固件文件")
            return

        cfg_dict = self.device_panel.get_config_dict()
        config = FlashConfig(
            fw_path=fw_path,
            bitrate=cfg_dict["bitrate"],
            hw_compat_id=cfg_dict["hw_compat_id"],
            version=cfg_dict["version"],
            fd_mode=cfg_dict["fd_mode"],
            data_bitrate=cfg_dict["data_bitrate"],
            max_frame_size=cfg_dict["max_frame_size"],
            can_id=cfg_dict["can_id"],
        )

        # 禁用所有设备操作
        self.fw_panel.set_building(True)
        self.device_panel.device_list.setEnabled(False)
        self.device_panel.scan_btn.setEnabled(False)
        self.device_panel.connect_btn.setEnabled(False)

        # 先停下测试总线（不改变 UI 连接状态）
        if hasattr(self, '_test_bus') and self._test_bus is not None:
            try:
                self._test_bus.close()
            except Exception:
                pass
            self._test_bus = None

        # 创建工作线程
        self._worker = FlashWorker()
        self._worker_thread = QThread(self)
        self._worker.moveToThread(self._worker_thread)

        self._worker.progress.connect(self.fw_panel.on_progress)
        self._worker.log_message.connect(self.fw_panel.on_log)
        self._worker.finished.connect(self._on_flash_finished)
        self._worker.error_occurred.connect(self._on_flash_error)
        self._worker.bus_ready.connect(self._on_bus_ready)

        self._worker_thread.started.connect(lambda: self._worker.start_flash(config))
        self._worker_thread.finished.connect(self._worker.deleteLater)
        self._worker_thread.finished.connect(self._worker_thread.deleteLater)

        self._worker_thread.start()

    def _on_stop_flash(self):
        if self._worker is not None:
            self._worker.cancel()

    def _on_flash_finished(self, success: bool):
        self.fw_panel.on_finished(success)
        self.fw_panel.set_building(False)
        self.device_panel.device_list.setEnabled(True)
        self.device_panel.scan_btn.setEnabled(True)
        self.device_panel.connect_btn.setEnabled(True)
        if success:
            self.device_panel.set_connected()
            self.status_label.setText("升级完成 (USB 保持连接)")
        else:
            self.device_panel.set_disconnected()
            self.status_label.setText("升级失败")

        if self._worker_thread is not None:
            self._worker_thread.quit()
            self._worker_thread.wait()
            self._worker_thread = None
        self._worker = None

    def _on_flash_error(self, msg: str):
        self.fw_panel.on_log(f"[ERROR] {msg}")

    def _on_bus_ready(self, bus):
        """Worker 成功后传回 ZDTCanable 实例，主窗口接管。"""
        if hasattr(self, '_test_bus') and self._test_bus is not None:
            try:
                self._test_bus.close()
            except Exception:
                pass
        self._test_bus = bus

    def _show_about(self):
        QMessageBox.about(self, "关于",
            "<b>STM32 CAN Bootloader Flash Tool</b><br><br>"
            "基于 CANable 2.5 (USB-CAN) 的固件升级上位机。<br><br>"
            "协议: 双分区 A/B CAN 固件升级 @ boot_protocol_spec.md<br>"
            "SDK: canable_sdk (ElmueSoft 协议)<br>"
            "GUI: PySide6")

    def closeEvent(self, e):
        if self._worker is not None:
            self._worker.cancel()
            if self._worker_thread is not None:
                self._worker_thread.quit()
                self._worker_thread.wait(1000)
        super().closeEvent(e)
