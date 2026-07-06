"""CANable 2.5 主窗口。"""
from __future__ import annotations

import os
import csv
import json
from threading import Thread
from typing import List, Optional

from PySide6.QtCore import Qt, QTimer, QThread, Slot, Signal
from PySide6.QtGui import QAction, QActionGroup, QIcon, QKeySequence, QShortcut
from PySide6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QMenu, QApplication,
                                QLabel, QComboBox, QPushButton, QSplitter,
                                QListWidget, QListWidgetItem, QTabWidget,
                                QDockWidget, QStatusBar, QToolBar, QFileDialog,
                                QMessageBox, QInputDialog, QGroupBox, QFormLayout,
                                QDoubleSpinBox, QCheckBox)

from PySide6.QtCore import QMutexLocker

from canable_sdk import ZDTCanable, CANFrame
from .style import set_theme, get_qss, current_theme, FG_ACCENT, FG_DIM, FG_WARN, FG_ERROR
from .i18n import _, language_changed, get_language, set_language
from .worker import CANWorker
from .trace import TracePanel
from .send import SendPanel
from .filters import FilterPanel

# --------------------------------------------------------------------------- #
#  主窗口
# --------------------------------------------------------------------------- #
class MainWindow(QMainWindow):
    APP_NAME = "CANable2.5"
    ORG_NAME = "canable"
    WINDOW_TITLE = "CANable 2.5"

    BITRATES = [10_000, 20_000, 50_000, 100_000, 125_000, 250_000,
                333_000, 500_000, 800_000, 1_000_000]

    def __init__(self):
        super().__init__()
        self.setWindowTitle("CANable 2.5")
        self.resize(1400, 850)
        self.setDockOptions(self.dockOptions() & ~QMainWindow.AnimatedDocks)

        self._settings_dirty = False
        self._settings_timer = QTimer(self)
        self._settings_timer.setSingleShot(True)
        self._settings_timer.timeout.connect(self._flush_settings)

        # 状态
        self._connected = False
        self._worker: Optional[CANWorker] = None
        self._frame_count = 0
        self._last_load = 0.0
        self._last_fps = 0
        self._noack_warning = False
        self._noack_timer = QTimer(self)
        self._noack_timer.setSingleShot(True)
        self._noack_timer.timeout.connect(self._clear_noack)
        self._connected_msg = ""
        self._settings = {}
        self.__init_ui()

    def _settings_path(self):
        return os.path.join(os.path.dirname(self.send_panel.csv_path()), "settings.json")

    def _set(self, key, value):
        self._settings[key] = value
        if not self._settings_dirty:
            self._settings_dirty = True
            self._settings_timer.start(2000)

    def _flush_settings(self):
        if not self._settings_dirty:
            return
        self._settings_dirty = False
        self._settings_timer.stop()
        try:
            with open(self._settings_path(), "w", encoding="utf-8") as f:
                json.dump(self._settings, f, indent=2)
        except Exception:
            pass

    def _get(self, key, default=None):
        return self._settings.get(key, default)

    def __init_ui(self):
        self._build_ui()
        self._build_menubar()
        self._build_statusbar()
        self._wire_signals()
        self._restore_settings()

    # ------------------------------------------------------------------ UI
    def _build_ui(self):
        # 中心：Tab
        self.trace_panel  = TracePanel(self)
        self.trace_panel.view.setAlternatingRowColors(True)
        center_tabs = QTabWidget()
        center_tabs.addTab(self.trace_panel, "Trace")
        self._center_tabs = center_tabs

        # 左侧：设备 + 总线
        left = self._build_left_panel()

        # 主分割
        self.splitter = QSplitter(Qt.Horizontal)
        self.splitter.addWidget(left)
        self.splitter.addWidget(center_tabs)
        self.splitter.setStretchFactor(0, 0)
        self.splitter.setStretchFactor(1, 1)
        self.splitter.setSizes([260, 1140])
        self.setCentralWidget(self.splitter)

        # 底部 Send Dock
        self.send_panel = SendPanel(self)
        self.send_dock = QDockWidget(_("Window.SendMessages"), self)
        self.send_dock.setObjectName("SendDock")
        self.send_dock.setWidget(self.send_panel)
        self.send_dock.setFeatures(
            QDockWidget.DockWidgetMovable | QDockWidget.DockWidgetClosable)
        self.send_dock.setAllowedAreas(Qt.BottomDockWidgetArea | Qt.RightDockWidgetArea)
        self.addDockWidget(Qt.BottomDockWidgetArea, self.send_dock)

        # 右侧 Filter Dock
        self.filter_panel = FilterPanel(self)
        self.filter_dock = QDockWidget(_("Window.Filters"), self)
        self.filter_dock.setObjectName("FilterDock")
        self.filter_dock.setWidget(self.filter_panel)
        self.filter_dock.setFeatures(
            QDockWidget.DockWidgetMovable | QDockWidget.DockWidgetClosable)
        self.filter_dock.setAllowedAreas(Qt.RightDockWidgetArea | Qt.BottomDockWidgetArea)
        self.addDockWidget(Qt.RightDockWidgetArea, self.filter_dock)

    def _build_left_panel(self) -> QWidget:
        w = QWidget()
        w.setObjectName("sidebar")
        layout = QVBoxLayout(w)
        layout.setContentsMargins(4, 4, 4, 4)

        # 总线配置
        self._bus_box = QGroupBox(_("Left.Bus"))
        bf = QFormLayout(self._bus_box)
        self.bitrate_combo = QComboBox()
        for b in self.BITRATES:
            self.bitrate_combo.addItem(f"{b:,} bps", b)
        self.bitrate_combo.setCurrentText("500,000 bps")
        self._lbl_bitrate = QLabel(_("Left.Bitrate"))
        bf.addRow(self._lbl_bitrate, self.bitrate_combo)

        # CAN FD 选项
        self.fd_chk = QCheckBox("CAN FD")
        self.fd_chk.toggled.connect(self._on_fd_toggle)
        bf.addRow("", self.fd_chk)

        self.data_bitrate_combo = QComboBox()
        self.data_bitrate_combo.addItem("1,000,000 bps", 1_000_000)
        self.data_bitrate_combo.addItem("2,000,000 bps", 2_000_000)
        self.data_bitrate_combo.addItem("4,000,000 bps", 4_000_000)
        self.data_bitrate_combo.addItem("5,000,000 bps", 5_000_000)
        self.data_bitrate_combo.addItem("8,000,000 bps", 8_000_000)
        self.data_bitrate_combo.setEnabled(False)
        self._lbl_data_bitrate = QLabel(_("Left.DataBitrate"))
        bf.addRow(self._lbl_data_bitrate, self.data_bitrate_combo)

        self.sample_combo = QComboBox()
        self.sample_combo.addItems(["87.5% (default)", "75.0%", "66.7%", "50.0%"])
        self._lbl_sample = QLabel(_("Left.SamplePoint"))
        bf.addRow(self._lbl_sample, self.sample_combo)
        layout.addWidget(self._bus_box)

        # 设备
        self._dev_box = QGroupBox(_("Left.Devices"))
        dv = QVBoxLayout(self._dev_box)
        self.device_list = QListWidget()
        dv.addWidget(self.device_list)
        self.scan_btn = QPushButton(_("Left.Scan"))
        self.scan_btn.clicked.connect(self._scan_devices)
        dv.addWidget(self.scan_btn)
        layout.addWidget(self._dev_box, 1)

        # 设备状态卡片
        # 操作
        self._act_box = QGroupBox(_("Left.QuickActions"))
        self._act_box.setObjectName("quickActions")
        av = QVBoxLayout(self._act_box)
        self.connect_btn = QPushButton(_("Left.Connect"))
        self.connect_btn.setObjectName("connectBtn")
        self.connect_btn.setCheckable(True)
        self.connect_btn.clicked.connect(self._on_connect_toggle)
        av.addWidget(self.connect_btn)

        layout.addWidget(self._act_box)

        return w

    def _build_menubar(self):
        mb = self.menuBar()

        self._menu_actions = []
        # File
        self._menu_file = file_menu = mb.addMenu(_("Menu.File"))
        act_open = QAction(_("File.OpenTrace"), self)
        act_open.setShortcut(QKeySequence.Open)
        act_open.triggered.connect(self._on_load_trace)
        file_menu.addAction(act_open)
        act_save = QAction(_("File.SaveTrace"), self)
        act_save.setShortcut(QKeySequence.Save)
        act_save.triggered.connect(self._on_save_trace)
        file_menu.addAction(act_save)
        file_menu.addSeparator()
        act_save_send = QAction(_("File.SaveSendList"), self)
        act_save_send.triggered.connect(self._on_save_send_list)
        file_menu.addAction(act_save_send)
        act_load_send = QAction(_("File.LoadSendList"), self)
        act_load_send.triggered.connect(self._on_load_send_list)
        file_menu.addAction(act_load_send)
        file_menu.addSeparator()
        act_quit = QAction(_("File.Exit"), self)
        act_quit.setShortcut(QKeySequence.Quit)
        act_quit.triggered.connect(self.close)
        file_menu.addAction(act_quit)

        # View
        view_menu = mb.addMenu(_("Menu.Windows"))
        self.act_toggle_send = self.send_dock.toggleViewAction()
        self.act_toggle_send.setText(_("Window.SendMessages"))
        self.act_toggle_filter = self.filter_dock.toggleViewAction()
        self.act_toggle_filter.setText(_("Window.Filters"))
        view_menu.addAction(self.act_toggle_send)
        view_menu.addAction(self.act_toggle_filter)

        # Hardware
        hw_menu = mb.addMenu(_("Menu.Hardware"))
        act_scan = QAction(_("HW.ScanDevices"), self)
        act_scan.triggered.connect(self._scan_devices)
        hw_menu.addAction(act_scan)
        hw_menu.addSeparator()
        # ElmueSoft 协议 ListenOnly 模式：只听不发，不发送 ACK
        # 注意：想要"自己发自己收"必须接两个 CAN 节点。

        # Tools
        tools_menu = mb.addMenu(_("Menu.Tools"))
        act_send_once = QAction("发送单帧…", self)
        act_send_once.setShortcut("Ctrl+Return")
        act_send_once.triggered.connect(self._on_quick_send)
        tools_menu.addAction(act_send_once)

        # Language submenu
        theme_menu = tools_menu.addMenu(_("Menu.Theme"))
        self.act_theme_light = QAction(_("Theme.Light"), self, checkable=True)
        self.act_theme_dark = QAction(_("Theme.Dark"), self, checkable=True)
        self.act_theme_light.setChecked(current_theme() == "light")
        self.act_theme_dark.setChecked(current_theme() == "dark")
        theme_group = QActionGroup(self)
        theme_group.setExclusive(True)
        theme_group.addAction(self.act_theme_light)
        theme_group.addAction(self.act_theme_dark)
        theme_menu.addAction(self.act_theme_light)
        theme_menu.addAction(self.act_theme_dark)
        theme_group.triggered.connect(self._on_theme_changed)

        lang_menu = tools_menu.addMenu(_("Menu.Language"))
        self.act_lang_zh = QAction("中文", self, checkable=True)
        self.act_lang_en = QAction("English", self, checkable=True)
        self.act_lang_zh.setChecked(get_language() == "zh")
        self.act_lang_en.setChecked(get_language() != "zh")
        lang_group = QActionGroup(self)
        lang_group.setExclusive(True)
        lang_group.addAction(self.act_lang_zh)
        lang_group.addAction(self.act_lang_en)
        lang_menu.addAction(self.act_lang_zh)
        lang_menu.addAction(self.act_lang_en)
        lang_group.triggered.connect(self._on_language_changed)

        # Help
        help_menu = mb.addMenu(_("Menu.Help"))
        act_about = QAction(_("Help.About"), self)
        act_about.triggered.connect(self._on_about)
        help_menu.addAction(act_about)



        # register actions for language refresh
        self._menu_actions = [
            (theme_menu, "Menu.Theme"),
            (lang_menu, "Menu.Language"),
            (self._menu_file, "Menu.File"),
            (view_menu, "Menu.Windows"),
            (hw_menu, "Menu.Hardware"),
            (tools_menu, "Menu.Tools"),
            (help_menu, "Menu.Help"),
            (act_open, "File.OpenTrace"),
            (act_save, "File.SaveTrace"),
            (act_save_send, "File.SaveSendList"),
            (act_load_send, "File.LoadSendList"),
            (act_quit, "File.Exit"),
            (act_scan, "HW.ScanDevices"),
            (act_send_once, "Tools.QuickSend"),
            (act_about, "Help.About"),
            (self.act_theme_light, "Theme.Light"),
            (self.act_theme_dark, "Theme.Dark"),
        ]
        QShortcut(QKeySequence("Ctrl+L"), self, self.trace_panel.clear_all)


    def _build_statusbar(self):
        sb: QStatusBar = self.statusBar()
        self.status_label = QLabel(_("Status.Disconnected"))
        self.status_label.setObjectName("statusLabel")
        self.status_label.setProperty("connected", False)
        sb.addWidget(self.status_label)

        sb.addPermanentWidget(QLabel("  "))
        self.bitrate_label = QLabel("— bps")
        sb.addPermanentWidget(self.bitrate_label)

        sb.addPermanentWidget(QLabel("  |  "))
        self.fps_label = QLabel(f"0 {_("Status.FPS")}")
        sb.addPermanentWidget(self.fps_label)

        sb.addPermanentWidget(QLabel("  |  "))
        self.load_label = QLabel(f"{_("Status.Load")} 0%")
        self.load_label.setObjectName("busLoad")
        sb.addPermanentWidget(self.load_label)

        sb.addPermanentWidget(QLabel("  |  "))
        self.count_label = QLabel(f"{_("Status.TotalFrames")}: 0")
        sb.addPermanentWidget(self.count_label)

    # ------------------------------------------------------------ 信号
    def _wire_signals(self):
        # Send → Worker
        self.send_panel.request_send.connect(self._on_send_frame)
        # Filter → Worker
        self.filter_panel.filters_changed.connect(self._on_filters_changed)
        # Bitrate change in side panel
        self.bitrate_combo.currentIndexChanged.connect(self._on_bitrate_combo_changed)

    # ----------------------------------------------------------- 设备扫描
    @Slot()
    def _scan_devices(self):
        self.device_list.clear()
        try:
            devs = ZDTCanable.list_devices()
        except Exception as e:
            QMessageBox.critical(self, _("Left.Scan"), f"{_('Scan.Failed')}: {e}")
            return
        if not devs:
            item = QListWidgetItem("未发现 candleLight 设备")
            item.setFlags(item.flags() & ~Qt.ItemIsEnabled)
            self.device_list.addItem(item)
            return
        for d in devs:
            label = f"{d['manufacturer']} {d['product']}\n  S/N: {d['serial']}"
            li = QListWidgetItem(label)
            li.setData(Qt.UserRole, d)
            self.device_list.addItem(li)
        self.device_list.setCurrentRow(0)

    # ----------------------------------------------------------- CAN FD
    @Slot(bool)
    def _on_fd_toggle(self, enabled: bool):
        self.data_bitrate_combo.setEnabled(enabled)
        # 通知 send 面板更新 DLC 范围
        self.send_panel.set_fd_mode(enabled)

    # ----------------------------------------------------------- 连接
    @Slot()
    def _on_connect_toggle(self):
        if self._connected:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        if self._worker is not None:
            return
        # 注意：worker 不能有 parent，否则 moveToThread 会被拒绝
        self._worker = CANWorker()
        self._worker.bitrate = self.bitrate_combo.currentData()
        self._worker.fd_mode = self.fd_chk.isChecked()
        if self.fd_chk.isChecked():
            self._worker.data_bitrate = self.data_bitrate_combo.currentData()
        self._worker.frame_received.connect(self._on_frame_received)
        self._worker.state_changed.connect(self._on_state_changed)
        self._worker.error.connect(self._on_error)
        self._worker.bus_stats.connect(self._on_bus_stats)
        self._worker.noack_warning.connect(self._on_noack_warning)
        # 同步过滤器
        self._worker.set_filters(self.filter_panel.filters)
        # 在独立线程跑 worker 的 connect() + run() 循环
        self._worker_thread = QThread(self)
        self._worker.moveToThread(self._worker_thread)
        self._worker_thread.started.connect(self._worker.connect)
        self._worker_thread.started.connect(self._worker.run)
        # 线程结束时清理 worker
        self._worker_thread.finished.connect(self._worker.deleteLater)
        self._worker_thread.finished.connect(self._worker_thread.deleteLater)
        self._worker_thread.start()
        self._update_connect_ui(True, "正在连接…")

    def _disconnect(self):
        if self._worker is None:
            return
        # 通知 worker 线程退出（run() 的 finally 块会关闭设备并 emit state_changed）
        with QMutexLocker(self._worker._mutex):
            self._worker._running = False
            self._worker._connected = False
        if hasattr(self, "_worker_thread") and self._worker_thread is not None:
            self._worker_thread.wait(500)
            self._worker_thread = None
        self._worker = None
        self._update_connect_ui(False, "已断开")

    def _update_connect_ui(self, connected: bool, msg: str):
        self._connected = connected
        if connected:
            self._connected_msg = msg
        self.status_label.setText(msg)
        self.status_label.setProperty("connected", connected)
        # 重新应用属性样式
        self.status_label.style().unpolish(self.status_label)
        self.status_label.style().polish(self.status_label)
        self.connect_btn.setChecked(connected)
        self.connect_btn.setText(_("Left.Disconnect") if connected else _("Left.Connect"))
        self.bitrate_combo.setEnabled(not connected)
        self.bitrate_label.setText(f"{self.bitrate_combo.currentData():,} bps" if connected else "— bps")

    @Slot(bool, str)
    def _on_state_changed(self, connected: bool, msg: str):
        if connected:
            self._update_connect_ui(True, msg)
        else:
            self._update_connect_ui(False, msg)

    @Slot(str)
    def _on_error(self, err: str):
        self.status_label.setText(err)
        self.status_label.setProperty("connected", False)
        self.status_label.style().unpolish(self.status_label)
        self.status_label.style().polish(self.status_label)
        self.count_label.setText(f"⚠ {err[:60]}")

    @Slot(str)
    def _on_noack_warning(self, msg: str):
        self._noack_warning = True
        self.status_label.setText(f"⚠ {_('Status.NoAck')}")
        self.status_label.setProperty("connected", False)
        self.status_label.style().unpolish(self.status_label)
        self.status_label.style().polish(self.status_label)
        self._noack_timer.start(1000)

    def _clear_noack(self):
        self._noack_warning = False
        self._update_connect_ui(self._connected, self._connected_msg)

    @Slot(object)
    def _on_frame_received(self, frame: CANFrame):
        self._frame_count += 1
        self.trace_panel.append_frame(frame)

    @Slot(float, int)
    def _on_bus_stats(self, load: float, fps: int):
        self._last_load = load
        self._last_fps = fps
        self.fps_label.setText(f"{fps} fps")
        self.load_label.setText(f"负载 {load:.1f}%")
        if load < 40:
            self.load_label.setProperty("level", "low")
        elif load < 75:
            self.load_label.setProperty("level", "mid")
        else:
            self.load_label.setProperty("level", "high")
        self.load_label.style().unpolish(self.load_label)
        self.load_label.style().polish(self.load_label)
        self.count_label.setText(f"{_("Status.TotalFrames")}: {self._frame_count}")



    def _on_bitrate_combo_changed(self, idx: int):
        br = self.bitrate_combo.itemData(idx)
        if br is None: return
        if self._worker is not None:
            self._worker.set_bitrate_slot(br)

    @Slot(list)
    def _on_filters_changed(self, filters):
        if self._worker is not None:
            self._worker.set_filters(filters)

    @Slot(object)
    def _on_send_frame(self, frame: CANFrame):
        if self._worker is None:
            self.status_label.setText("未连接，无法发送")
            return
        self._worker.send(frame)

    # ------------------------------------------------------------- 文件
    def _on_save_trace(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "保存 Trace", "trace.csv",
            "CSV (*.csv);;JSON Lines (*.jsonl);;ASC (*.asc)")
        if not path:
            return
        if path.endswith(".csv"):
            with open(path, "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(["time", "ch", "can_id", "ext", "rtr", "dlc", "data_hex"])
                for fr in self.trace_panel.view._model._rows:
                    w.writerow([f"{fr.timestamp:.6f}", "CAN1",
                                f"{fr.can_id:X}", int(fr.extended),
                                int(fr.rtr), fr.dlc, fr.data.hex(' ').upper()])
        elif path.endswith(".jsonl"):
            with open(path, "w", encoding="utf-8") as f:
                for fr in self.trace_panel.view._model._rows:
                    f.write(json.dumps({
                        "time": fr.timestamp, "can_id": fr.can_id,
                        "extended": fr.extended, "rtr": fr.rtr,
                        "dlc": fr.dlc, "data": fr.data.hex(),
                    }) + "\n")
        else:
            # Vector ASC 风格（简化）
            with open(path, "w", encoding="utf-8") as f:
                f.write("date Wed Jan 01 00:00:00.000 2025\n")
                f.write("base hex  timestamps absolute\n")
                f.write("internal events logged\n")
                f.write("// version 13.0.0\n")
                f.write("Begin Triggerblock\n")
                t0 = self.trace_panel.view._model._rows[0].timestamp \
                    if self.trace_panel.view._model._rows else 0.0
                for fr in self.trace_panel.view._model._rows:
                    ts = fr.timestamp - t0
                    if fr.extended:
                        head = f"{fr.can_id:08X}x"
                    else:
                        head = f"{fr.can_id:03X}x" if fr.rtr else f"{fr.can_id:03X}"
                    d = " ".join(f"{b:02X}" for b in fr.data[:fr.dlc]) or "R"
                    f.write(f"{ts:9.6f} CAN1 {head} {d} Length {fr.dlc} CRC OK\n")
                f.write("End TriggerBlock\n")
        self.status_label.setText(f"Trace 已保存到 {path}")

    def _on_load_trace(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "加载 Trace", "", "CSV (*.csv);;JSON Lines (*.jsonl);;ASC (*.asc)")
        if not path:
            return
        try:
            if path.endswith(".csv"):
                with open(path, "r", encoding="utf-8") as f:
                    r = csv.DictReader(f)
                    for row in r:
                        fr = CANFrame(
                            can_id=int(row["can_id"], 16),
                            data=bytes.fromhex(row["data_hex"].replace(" ", "")),
                            extended=bool(int(row["ext"])),
                            rtr=bool(int(row["rtr"])),
                            timestamp=float(row["time"]),
                        )
                        self.trace_panel.append_frame(fr)
            elif path.endswith(".jsonl"):
                with open(path, "r", encoding="utf-8") as f:
                    for line in f:
                        d = json.loads(line)
                        fr = CANFrame(
                            can_id=d["can_id"],
                            data=bytes.fromhex(d["data"]),
                            extended=d.get("extended", False),
                            rtr=d.get("rtr", False),
                            timestamp=d["time"],
                        )
                        self.trace_panel.append_frame(fr)
            else:
                QMessageBox.information(self, _("File.OpenTrace"), "ASC 文件请使用回放工具，暂不实现。")
        except Exception as e:
            QMessageBox.critical(self, _("File.LoadSendList"), f"{_('Load.Failed')}: {e}")

    def _on_save_send_list(self):
        self.send_panel.to_csv(self.send_panel.csv_path())
        self.status_label.setText(f"发送列表已保存: {self.send_panel.csv_path()}")

    def _on_load_send_list(self):
        if not self.send_panel.exists():
            self.status_label.setText("无历史记录")
            return
        try:
            self.send_panel.from_csv(self.send_panel.csv_path())
            self.status_label.setText(f"发送列表已加载: {self.send_panel.csv_path()}")
        except Exception as e:
            QMessageBox.critical(self, _("File.LoadSendList"), f"{_('Load.Failed')}: {e}")

    # ------------------------------------------------------------ 快速发送
    def _on_quick_send(self):
        text, ok = QInputDialog.getText(
            self, _("Tools.QuickSend"),
            "格式: ID,HEX_DATA    例:  0x123,DE AD BE EF 0A",
        )
        if not ok or not text.strip():
            return
        try:
            parts = [p.strip() for p in text.split(",")]
            can_id = int(parts[0], 16)
            data = bytes.fromhex(parts[1].replace(" ", "")) if len(parts) > 1 else b""
            extended = can_id > 0x7FF
            frame = CANFrame(can_id, data, extended=extended)
            self._on_send_frame(frame)
            self.status_label.setText(f"已发送: {frame}")
        except Exception as e:
            QMessageBox.warning(self, _("File.OpenTrace"), f"{_('Format.Error')}: {e}")

    # ------------------------------------------------------------ 关闭
    def _on_about(self):
        QMessageBox.information(self, _("Help.About"),
            "<b>CANable 2.5</b><br>" + _("About.Desc") + "<br><br>" + _("About.Tech"))

    def _restore_settings(self):
        try:
            with open(self._settings_path(), encoding="utf-8") as f:
                self._settings = json.load(f)
        except Exception:
            self._settings = {}
        s = self._settings
        bitrate = s.get("bitrate", 500_000)
        if bitrate in self.BITRATES:
            self.bitrate_combo.setCurrentIndex(self.bitrate_combo.findData(bitrate))
        self.fd_chk.setChecked(s.get("fd_mode", False))
        data_br = s.get("data_bitrate", 1_000_000)
        idx = self.data_bitrate_combo.findData(data_br)
        if idx >= 0:
            self.data_bitrate_combo.setCurrentIndex(idx)
        self.trace_panel.autoscroll_chk.setChecked(s.get("autoscroll", True))
        if s.get("collapse", False):
            self.trace_panel.collapse_chk.setChecked(True)
        lang = s.get("language", "zh")
        theme = s.get("theme", "light")
        set_theme(theme)
        QApplication.instance().setStyleSheet(get_qss())
        set_language(lang)
        if hasattr(self, "act_lang_zh"):
            self.act_lang_zh.setChecked(lang == "zh")
            self.act_lang_en.setChecked(lang != "zh")
        try:
            self.filter_panel.from_dict_list(s.get("filters", []))
        except Exception:
            pass
        splitter_hex = s.get("splitter")
        if splitter_hex:
            self.splitter.restoreState(bytes.fromhex(splitter_hex))
        trace_hdr = s.get("trace_hdr")
        if trace_hdr:
            self.trace_panel.view.horizontalHeader().restoreState(bytes.fromhex(trace_hdr))
        send_hdr = s.get("send_hdr")
        if send_hdr:
            self.send_panel.table.horizontalHeader().restoreState(bytes.fromhex(send_hdr))
        filter_hdr = s.get("filter_hdr")
        if filter_hdr:
            self.filter_panel.table.horizontalHeader().restoreState(bytes.fromhex(filter_hdr))
        geometry = s.get("geometry")
        if geometry:
            self.restoreGeometry(bytes.fromhex(geometry))
        state = s.get("state")
        if state:
            self.restoreState(bytes.fromhex(state))
        # 自动扫描一次
        QTimer.singleShot(100, self._scan_devices)
        if self.send_panel.exists():
            try:
                self.send_panel.from_csv(self.send_panel.csv_path())
            except Exception:
                pass


    def _on_theme_changed(self, action):
        name = "dark" if action == self.act_theme_dark else "light"
        set_theme(name)
        self._set("theme", name)
        QApplication.instance().setStyleSheet(get_qss())

    def _on_language_changed(self, action):
        self._set("language", "zh" if action == self.act_lang_zh else "en")
        set_language("zh" if action == self.act_lang_zh else "en")
        self._refresh_language()

    def _refresh_language(self):
        self.setWindowTitle("CANable 2.5")
        # menu actions
        if hasattr(self, "_menu_actions"):
            for item, key in self._menu_actions:
                if type(item).__name__ == "QMenu":
                    item.setTitle(_(key))
                else:
                    item.setText(_(key))
        # group boxes
        self._bus_box.setTitle(_("Left.Bus"))
        self._dev_box.setTitle(_("Left.Devices"))
        self._act_box.setTitle(_("Left.QuickActions"))
        # buttons
        self.scan_btn.setText(_("Left.Scan"))
        self.connect_btn.setText(_("Left.Disconnect") if self._connected else _("Left.Connect"))
        # form labels
        self._lbl_bitrate.setText(_("Left.Bitrate"))
        self._lbl_data_bitrate.setText(_("Left.DataBitrate"))
        self._lbl_sample.setText(_("Left.SamplePoint"))
        # status bar
        if hasattr(self, "fps_label"):
            self.fps_label.setText(f"{self._last_fps} {_('Status.FPS')}")
            self.load_label.setText(f"{_('Status.Load')} {self._last_load:.0f}%")
            self.count_label.setText(f"{_('Status.TotalFrames')}: {self._frame_count}")
            self.status_label.setText(_("Status.Connected") if self._connected else _("Status.Disconnected"))
        # docks
        self.send_dock.setWindowTitle(_("Window.SendMessages"))
        self.filter_dock.setWindowTitle(_("Window.Filters"))
        if hasattr(self, "act_toggle_send"):
            self.act_toggle_send.setText(_("Window.SendMessages"))
            self.act_toggle_filter.setText(_("Window.Filters"))
        # sub-panels
        if hasattr(self, "trace_panel") and hasattr(self.trace_panel, "refresh_language"):
            self.trace_panel.refresh_language()
        if hasattr(self, "send_panel") and hasattr(self.send_panel, "refresh_language"):
            self.send_panel.refresh_language()
        if hasattr(self, "filter_panel") and hasattr(self.filter_panel, "refresh_language"):
            self.filter_panel.refresh_language()

    def closeEvent(self, e):
        if not hasattr(self, 'bitrate_combo'):
            e.accept()
            return
        self._set("bitrate", self.bitrate_combo.currentData())
        self._set("fd_mode", self.fd_chk.isChecked())
        self._set("data_bitrate", self.data_bitrate_combo.currentData())
        self._set("autoscroll", self.trace_panel.autoscroll_chk.isChecked())
        self._set("collapse", self.trace_panel.collapse_chk.isChecked())
        self._set("theme", current_theme())
        self._set("language", get_language())
        self._set("filters", self.filter_panel.to_dict_list())
        self._set("splitter", bytes(self.splitter.saveState().toHex()).decode())
        self._set("trace_hdr", bytes(self.trace_panel.view.horizontalHeader().saveState().toHex()).decode())
        self._set("send_hdr", bytes(self.send_panel.table.horizontalHeader().saveState().toHex()).decode())
        self._set("filter_hdr", bytes(self.filter_panel.table.horizontalHeader().saveState().toHex()).decode())
        self._set("geometry", bytes(self.saveGeometry().toHex()).decode())
        self._set("state", bytes(self.saveState().toHex()).decode())
        self._flush_settings()
        try:
            self.send_panel.to_csv(self.send_panel.csv_path())
        except Exception:
            pass
        if self._connected:
            self._disconnect()
        e.accept()
