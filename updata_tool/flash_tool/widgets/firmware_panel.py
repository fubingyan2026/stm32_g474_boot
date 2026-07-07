"""右侧面板：固件选择、升级控制、进度条、日志。"""
from __future__ import annotations

import os
import time

from PySide6.QtCore import Qt, Signal, Slot
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox,
    QPushButton, QLabel, QProgressBar, QTextEdit,
    QFileDialog, QMessageBox,
)

from ..protocol import compute_crc32


class FirmwarePanel(QWidget):
    start_requested = Signal()
    stop_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._fw_path: str = ""
        self._building = False
        self._build_ui()
        self._wire_signals()

    def _build_ui(self):
        layout = QVBoxLayout(self)

        # ── 固件选择 ──
        gb_fw = QGroupBox("固件文件")
        fw_layout = QVBoxLayout(gb_fw)

        fw_row = QHBoxLayout()
        self.fw_path_edit = QLabel("未选择文件")
        self.fw_path_edit.setWordWrap(True)
        fw_row.addWidget(self.fw_path_edit, 1)
        self.browse_btn = QPushButton("浏览...")
        fw_row.addWidget(self.browse_btn)
        fw_layout.addLayout(fw_row)

        fw_info_row = QHBoxLayout()
        self.fw_size_label = QLabel("大小: —")
        fw_info_row.addWidget(self.fw_size_label)
        self.fw_crc_label = QLabel("CRC32: —")
        fw_info_row.addWidget(self.fw_crc_label)
        fw_layout.addLayout(fw_info_row)

        layout.addWidget(gb_fw)

        # ── 升级控制 ──
        gb_ctrl = QGroupBox("升级控制")
        ctrl_layout = QVBoxLayout(gb_ctrl)

        self.start_btn = QPushButton("开始升级")
        self.start_btn.setEnabled(False)
        self.start_btn.setMinimumHeight(36)
        ctrl_layout.addWidget(self.start_btn)

        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        ctrl_layout.addWidget(self.progress_bar)

        self.status_label = QLabel("就绪")
        ctrl_layout.addWidget(self.status_label)

        layout.addWidget(gb_ctrl)

        # ── 日志 ──
        gb_log = QGroupBox("日志")
        log_layout = QVBoxLayout(gb_log)
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFontFamily("Consolas, monospace")
        self.log_text.setLineWrapMode(QTextEdit.NoWrap)
        log_layout.addWidget(self.log_text)

        clear_btn = QPushButton("清空日志")
        log_layout.addWidget(clear_btn)
        clear_btn.clicked.connect(self.log_text.clear)

        layout.addWidget(gb_log, 1)

    def _wire_signals(self):
        self.browse_btn.clicked.connect(self._browse_fw)
        self.start_btn.clicked.connect(self._on_start_click)

    @Slot()
    def _browse_fw(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "选择固件文件", "",
            "固件文件 (*.bin *.hex);;所有文件 (*)")
        if not path:
            return
        self._fw_path = path

        base = os.path.basename(path)
        self.fw_path_edit.setText(base)

        size = os.path.getsize(path)
        self.fw_size_label.setText(f"大小: {size:,} 字节 ({size/1024:.1f} KB)")

        with open(path, "rb") as f:
            data = f.read()
        crc = compute_crc32(data)
        self.fw_crc_label.setText(f"CRC32: 0x{crc:08X}")

        self.start_btn.setEnabled(True)

    @Slot()
    def _on_start_click(self):
        if self._building:
            self.stop_requested.emit()
        else:
            if not self._fw_path:
                QMessageBox.warning(self, "警告", "请先选择固件文件")
                return
            self.start_requested.emit()

    def get_fw_path(self) -> str:
        return self._fw_path

    def get_fw_size(self) -> int:
        return os.path.getsize(self._fw_path) if self._fw_path else 0

    @Slot(int, int)
    def on_progress(self, current: int, total: int):
        if total > 0:
            pct = int(current * 100 / total)
            self.progress_bar.setValue(pct)
            self.status_label.setText(f"Block {current}/{total} ({pct}%)")

    @Slot(str)
    def on_log(self, msg: str):
        ts = time.strftime("%H:%M:%S")
        level = msg[0] if len(msg) > 1 else "I"
        rest = msg[2:] if len(msg) > 2 else msg

        level_colors = {
            "E": "#E53935",
            "W": "#FDD835",
            "D": "#4FC3F7",
            "V": "#888888",
        }
        color = level_colors.get(level, "#A5D6A7")
        ts_color = "#888888"

        html = (f'<span style="color:{color};">{level}</span> '
                f'<span style="color:{ts_color};">({ts})</span> '
                f'<span style="color:{color};">{rest}</span>')

        cursor = self.log_text.textCursor()
        cursor.movePosition(cursor.MoveOperation.End)
        cursor.insertHtml(html + "<br>")
        scrollbar = self.log_text.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    @Slot(bool)
    def on_finished(self, success: bool):
        self._building = False
        self.start_btn.setText("开始升级")
        self.start_btn.setEnabled(True)
        self.browse_btn.setEnabled(True)
        if success:
            self.status_label.setText("✅ 升级完成")
        else:
            self.status_label.setText("❌ 升级失败")

    def set_building(self, building: bool):
        self._building = building
        self.start_btn.setText("取消" if building else "开始升级")
        self.start_btn.setEnabled(True)
        self.browse_btn.setEnabled(not building)

    def reset(self):
        self.progress_bar.setValue(0)
        self.status_label.setText("就绪")
