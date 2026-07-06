"""左侧面板：CAN 设备扫描、连接、总线参数配置。"""
from __future__ import annotations

from typing import Optional

from PySide6.QtCore import Qt, Signal, Slot
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QFormLayout, QHBoxLayout,
    QGroupBox, QComboBox, QPushButton, QLabel, QCheckBox,
    QListWidget, QListWidgetItem, QLineEdit, QSpinBox,
)

from canable_sdk import ZDTCanable

BITRATES = [10_000, 20_000, 50_000, 100_000, 125_000, 250_000,
            500_000, 800_000, 1_000_000]

DATA_BITRATES = [1_000_000, 2_000_000, 4_000_000, 5_000_000, 8_000_000]

FD_FRAME_SIZES = [8, 12, 16, 20, 24, 32, 48, 64]


class DevicePanel(QWidget):
    connected = Signal()
    disconnected = Signal()
    device_scan_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._connected = False
        self._build_ui()
        self._wire_signals()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        # ── 设备分组 ──
        gb_dev = QGroupBox("CAN 设备")
        dv = QVBoxLayout(gb_dev)

        self.device_list = QListWidget()
        dv.addWidget(self.device_list)

        scan_btn_row = QHBoxLayout()
        self.scan_btn = QPushButton("扫描设备")
        scan_btn_row.addWidget(self.scan_btn)
        self.connect_btn = QPushButton("连接")
        self.connect_btn.setCheckable(True)
        self.connect_btn.setEnabled(False)
        scan_btn_row.addWidget(self.connect_btn)
        dv.addLayout(scan_btn_row)

        self.status_label = QLabel("未连接")
        self.status_label.setStyleSheet("color: #888;")
        dv.addWidget(self.status_label)

        layout.addWidget(gb_dev)

        # ── 总线配置分组 ──
        gb_bus = QGroupBox("总线配置")
        bf = QFormLayout(gb_bus)

        self.bitrate_combo = QComboBox()
        for b in BITRATES:
            self.bitrate_combo.addItem(f"{b:,} bps", b)
        self.bitrate_combo.setCurrentText("1,000,000 bps")
        bf.addRow("标称波特率:", self.bitrate_combo)

        self.fd_chk = QCheckBox("CAN FD")
        bf.addRow("", self.fd_chk)

        self.data_bitrate_combo = QComboBox()
        for b in DATA_BITRATES:
            self.data_bitrate_combo.addItem(f"{b:,} bps", b)
        self.data_bitrate_combo.setEnabled(False)
        bf.addRow("数据相波特率:", self.data_bitrate_combo)

        self.frame_size_combo = QComboBox()
        for s in FD_FRAME_SIZES:
            label = f"{s} 字节{' (Classic CAN)' if s == 8 else ''}"
            self.frame_size_combo.addItem(label, s)
        self.frame_size_combo.setEnabled(False)
        bf.addRow("帧长度:", self.frame_size_combo)

        self.hw_id_edit = QLineEdit("0x0001")
        bf.addRow("HW Compat ID:", self.hw_id_edit)

        self.version_spin = QSpinBox()
        self.version_spin.setRange(1, 65535)
        self.version_spin.setValue(1)
        bf.addRow("固件版本:", self.version_spin)

        layout.addWidget(gb_bus)
        layout.addStretch(1)

    def _wire_signals(self):
        self.scan_btn.clicked.connect(self._scan_devices)
        self.connect_btn.clicked.connect(self._on_connect_toggle)
        self.fd_chk.toggled.connect(self._on_fd_toggle)

    @Slot()
    def _scan_devices(self):
        self.device_list.clear()
        try:
            devs = ZDTCanable.list_devices()
        except Exception as e:
            item = QListWidgetItem(f"扫描失败: {e}")
            item.setFlags(item.flags() & ~Qt.ItemIsEnabled)
            self.device_list.addItem(item)
            return

        if not devs:
            item = QListWidgetItem("未发现 CANable 设备")
            item.setFlags(item.flags() & ~Qt.ItemIsEnabled)
            self.device_list.addItem(item)
            return

        for d in devs:
            label = f"{d.get('manufacturer','') or 'CANable'} {d.get('product','') or ''}\n  S/N: {d.get('serial','?')}"
            li = QListWidgetItem(label.strip())
            li.setData(Qt.UserRole, d)
            self.device_list.addItem(li)

        self.device_list.setCurrentRow(0)
        self.connect_btn.setEnabled(True)
        self.device_scan_requested.emit()

    @Slot(bool)
    def _on_fd_toggle(self, enabled: bool):
        self.data_bitrate_combo.setEnabled(enabled)
        self.frame_size_combo.setEnabled(enabled)
        if not enabled and self.frame_size_combo.currentData() != 8:
            self.frame_size_combo.setCurrentIndex(0)

    @Slot()
    def _on_connect_toggle(self):
        if self._connected:
            self.disconnected.emit()
        else:
            self.connected.emit()

    @Slot()
    def set_connected(self):
        self._connected = True
        self.connect_btn.setChecked(True)
        self.connect_btn.setText("断开")
        self.status_label.setText("已连接")
        self.status_label.setStyleSheet("color: #0a0; font-weight: bold;")
        self.scan_btn.setEnabled(False)
        self.device_list.setEnabled(False)

    @Slot()
    def set_disconnected(self):
        self._connected = False
        self.connect_btn.setChecked(False)
        self.connect_btn.setText("连接")
        self.status_label.setText("未连接")
        self.status_label.setStyleSheet("color: #888;")
        self.scan_btn.setEnabled(True)
        self.device_list.setEnabled(True)

    def get_selected_device(self) -> Optional[dict]:
        item = self.device_list.currentItem()
        if item is None:
            return None
        return item.data(Qt.UserRole)

    def get_config_dict(self) -> dict:
        return {
            "bitrate": self.bitrate_combo.currentData(),
            "fd_mode": self.fd_chk.isChecked(),
            "data_bitrate": self.data_bitrate_combo.currentData(),
            "max_frame_size": self.frame_size_combo.currentData(),
            "hw_compat_id": int(self.hw_id_edit.text(), 16) if self.hw_id_edit.text().startswith("0x") else int(self.hw_id_edit.text()),
            "version": self.version_spin.value(),
        }
