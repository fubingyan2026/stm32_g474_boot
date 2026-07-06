"""Trace panel: CAN message stream table (cangaroo style)."""
from __future__ import annotations

from collections import deque
from typing import Deque, Dict, Tuple, Optional

import csv
import os
import sys
from datetime import datetime
from PySide6.QtCore import (Qt, QAbstractTableModel, QModelIndex, Signal,
                            QTimer)
from PySide6.QtGui import QColor, QBrush, QFont
from PySide6.QtWidgets import (QTableView, QHeaderView, QAbstractItemView,
                                QVBoxLayout, QHBoxLayout, QWidget, QPushButton,
                                QCheckBox, QSpinBox, QLabel)

from canable_sdk import CANFrame
from .i18n import _, language_changed
from .style import id_color, FG_DIM, FG_ACCENT


DEFAULT_MAX_ROWS = 1_000


class TraceModel(QAbstractTableModel):
    HEADERS = ["No.", "Time (s)", "Ch", "ID", "Type", "DLC", "Data (hex)", "ASCII",
               "dt (ms)", "Period (ms)", "Count"]

    COL_INDEX, COL_TIME, COL_CH, COL_ID, COL_TYPE, COL_DLC, COL_DATA, \
        COL_ASCII, COL_DELTA, COL_PERIOD, COL_COUNT = range(11)

    def __init__(self, max_rows: int = DEFAULT_MAX_ROWS, parent=None):
        super().__init__(parent)
        self._header_labels = self._make_headers()
        self._rows: Deque[CANFrame] = deque(maxlen=max_rows)
        self._meta: Deque[dict]     = deque(maxlen=max_rows)
        self._text: Deque[list]     = deque(maxlen=max_rows)
        self._counts: Dict[Tuple[int, bool], int] = {}
        self._last_ts: Dict[Tuple[int, bool], float] = {}
        self._period:  Dict[Tuple[int, bool], float] = {}
        self._collapse = False
        self._id_vis:  Dict[Tuple[int, bool], int] = {}

    # ---------- Qt model interface ---------- #
    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self._rows)

    def columnCount(self, parent=QModelIndex()):
        return len(self._header_labels)

    def headerData(self, section, orientation, role=Qt.DisplayRole):
        if role != Qt.DisplayRole:
            return None
        if orientation == Qt.Horizontal:
            return self._header_labels[section] if section < len(self._header_labels) else None
        return None

    @staticmethod
    def _format_row(frame: CANFrame, meta: dict) -> list:
        t = frame.timestamp % 1000.0
        ch = "ERR" if frame.is_error else ("TX" if frame.is_tx else "CAN1")
        if frame.is_error:
            cid = "ERROR"
            typ = "ERR"
            dlc = ""
            data = frame._error_info
            ascii = frame._error_info
        else:
            cid = f"{frame.can_id:08X}" if frame.extended else f"{frame.can_id:03X}"
            if frame.rtr:
                typ = "RTR"
            elif frame.fd and frame.brs:
                typ = "FD+BRS"
            elif frame.fd:
                typ = "FD"
            elif frame.extended:
                typ = "Ext"
            else:
                typ = "Std"
            dlc = str(frame.dlc)
            data = frame.data.hex(' ').upper()
            ascii = ''.join(chr(b) if 32 <= b < 127 else '.' for b in frame.data)
        return [
            "",                         # COL_INDEX — set on insert
            f"{t:12.6f}",
            ch,
            cid,
            typ,
            dlc,
            data,
            ascii,
            f"{meta['dt']:.1f}" if meta['dt'] else "",
            f"{meta['period']:.1f}" if meta['period'] else "",
            str(meta['count']),
        ]

    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid():
            return None
        row = index.row()
        if row >= len(self._rows):
            return None
        f = self._rows[row]
        col = index.column()

        if role == Qt.DisplayRole and col < len(self._text[row]):
            if col == self.COL_INDEX:
                return f"{row + 1}"
            return self._text[row][col]

        if role == Qt.BackgroundRole:
            if f.is_error:
                return QBrush(QColor(255, 220, 220))
            if f.is_tx:
                return QBrush(QColor("#D2F0E3"))

        if role == Qt.ForegroundRole:
            if f.is_tx:
                return QBrush(QColor(FG_ACCENT))
            if f.rtr:
                return QBrush(QColor(FG_DIM))
            if not f.is_error:
                return QBrush(QColor(id_color(f.can_id, f.extended)))

        if role == Qt.TextAlignmentRole:
            if col in (self.COL_DLC, self.COL_COUNT):
                return Qt.AlignCenter

        if role == Qt.ToolTipRole:
            return f"ID: 0x{f.can_id:X}  DLC: {f.dlc}\nData: {f.data.hex(' ').upper()}"

        return None

    # ---------- public methods ---------- #
    def add_frame(self, frame: CANFrame):
        cid = (frame.can_id, frame.extended)
        now = frame.timestamp
        self._counts[cid] = self._counts.get(cid, 0) + 1

        dt = 0.0
        if cid in self._last_ts:
            dt = (now - self._last_ts[cid]) * 1000.0
            old = self._period.get(cid, dt)
            self._period[cid] = old * 0.7 + dt * 0.3
        self._last_ts[cid] = now
        period = self._period.get(cid, 0.0)
        count = self._counts[cid]
        meta = {"dt": dt, "period": period, "count": count}
        txt = self._format_row(frame, meta)

        if self._collapse:
            vis_idx = self._id_vis.get(cid)
            if vis_idx is not None and vis_idx < len(self._rows):
                self._rows[vis_idx] = frame
                self._meta[vis_idx] = meta
                self._text[vis_idx] = txt
                self.dataChanged.emit(
                    self.index(vis_idx, 0),
                    self.index(vis_idx, self.columnCount() - 1),
                )
                return

        # append new row
        if len(self._rows) == self._rows.maxlen:
            self.beginRemoveRows(QModelIndex(), 0, 0)
            old = self._rows[0]
            self._rows.popleft()
            self._meta.popleft()
            self._text.popleft()
            old_cid = (old.can_id, old.extended)
            if old_cid in self._id_vis and self._id_vis[old_cid] == 0:
                del self._id_vis[old_cid]
            for k in list(self._id_vis.keys()):
                self._id_vis[k] -= 1
            self.endRemoveRows()

        pos = len(self._rows)
        self.beginInsertRows(QModelIndex(), pos, pos)
        self._rows.append(frame)
        self._meta.append(meta)
        self._text.append(txt)
        self._id_vis[cid] = pos
        self.endInsertRows()

    def set_collapse_mode(self, on: bool):
        if self._collapse == on:
            return
        self._collapse = on
        self._id_vis.clear()
        if on:
            self.beginResetModel()
            collapsed_rows = deque(maxlen=self._rows.maxlen)
            collapsed_meta = deque(maxlen=self._rows.maxlen)
            collapsed_text = deque(maxlen=self._rows.maxlen)
            seen: Dict[Tuple[int, bool], int] = {}
            for i, f in enumerate(self._rows):
                cid = (f.can_id, f.extended)
                if cid in seen:
                    collapsed_rows[seen[cid]] = f
                    collapsed_meta[seen[cid]] = self._meta[i]
                    collapsed_text[seen[cid]] = self._format_row(f, self._meta[i])
                else:
                    idx = len(collapsed_rows)
                    seen[cid] = idx
                    self._id_vis[cid] = idx
                    collapsed_rows.append(f)
                    collapsed_meta.append(self._meta[i])
                    collapsed_text.append(self._text[i])
            self._rows = collapsed_rows
            self._meta = collapsed_meta
            self._text = collapsed_text
            self.endResetModel()

    @staticmethod
    def _make_headers():
        return [_("Trace.No"), _("Trace.Time"), _("Trace.Ch"), _("Trace.ID"), _("Trace.Type"), _("Trace.DLC"), _("Trace.Data"), _("Trace.ASCII"),
                _("Trace.Delta"), _("Trace.Period"), _("Trace.Count")]

    def update_headers(self):
        self._header_labels = self._make_headers()
        self.headerDataChanged.emit(Qt.Horizontal, 0, len(self._header_labels) - 1)

    def clear(self):
        self.beginResetModel()
        self._rows.clear()
        self._meta.clear()
        self._text.clear()
        self._counts.clear()
        self._last_ts.clear()
        self._period.clear()
        self._id_vis.clear()
        self.endResetModel()

    def id_summary(self):
        result = []
        for cid, count in self._counts.items():
            result.append((cid[0], cid[1], count, self._period.get(cid, 0.0)))
        result.sort(key=lambda x: x[0])
        return result


class TraceView(QTableView):
    def __init__(self, parent=None, max_rows: int = DEFAULT_MAX_ROWS):
        super().__init__(parent)
        self._model = TraceModel(max_rows=max_rows, parent=self)
        self.setModel(self._model)
        self.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.setShowGrid(False)
        self.verticalHeader().setVisible(False)
        self.verticalHeader().setDefaultSectionSize(18)
        self.setAlternatingRowColors(True)
        self.setSortingEnabled(False)

        font = QFont("Consolas", 9)
        font.setFamilies(["Consolas", "Noto Sans Mono CJK SC", "Liberation Mono", "Courier New", "monospace"])
        font.setStyleHint(QFont.Monospace)
        self.setFont(font)

        self._auto_scroll = True

        h = self.horizontalHeader()
        h.setHighlightSections(False)
        widths = {
            TraceModel.COL_INDEX:  60,
            TraceModel.COL_TIME:  120,
            TraceModel.COL_CH:    50,
            TraceModel.COL_ID:   110,
            TraceModel.COL_TYPE:  65,
            TraceModel.COL_DLC:   40,
            TraceModel.COL_DATA:  220,
            TraceModel.COL_ASCII: 90,
            TraceModel.COL_DELTA: 70,
            TraceModel.COL_PERIOD:95,
            TraceModel.COL_COUNT: 60,
        }
        for col, w in widths.items():
            h.resizeSection(col, w)
        for c in range(len(TraceModel._make_headers())):
            h.setSectionResizeMode(c, QHeaderView.Interactive)
        h.setSectionResizeMode(TraceModel.COL_DATA, QHeaderView.Stretch)
        h.setSectionResizeMode(TraceModel.COL_COUNT, QHeaderView.Fixed)
        h.setStretchLastSection(False)
        # per-column minimum widths based on header text
        fm = self.fontMetrics()
        self._col_mins = {}
        for c, hdr in enumerate(TraceModel._make_headers()):
            w = fm.horizontalAdvance(hdr) + 14
            self._col_mins[c] = max(w, 30)
        self.horizontalHeader().sectionResized.connect(self._clamp_width)


        self.verticalScrollBar().valueChanged.connect(self._on_scroll)
        self.verticalScrollBar().rangeChanged.connect(self._on_range)

    def _on_scroll(self, _):
        sb = self.verticalScrollBar()
        self._auto_scroll = (sb.value() >= sb.maximum() - 4)

    def _clamp_width(self, logicalIndex, oldSize, newSize):
        mn = self._col_mins.get(logicalIndex, 30)
        if newSize < mn:
            self.horizontalHeader().resizeSection(logicalIndex, mn)

    def _on_range(self, _min, _max):
        if self._auto_scroll:
            self.verticalScrollBar().setValue(_max)

    def add_frame(self, frame: CANFrame):
        self._model.add_frame(frame)

    def clear(self):
        self._model.clear()

    def set_collapse_mode(self, on: bool):
        self._model.set_collapse_mode(on)

    def id_summary(self):
        return self._model.id_summary()

    def selected_frames(self):
        rows = sorted({i.row() for i in self.selectionModel().selectedIndexes()})
        return [self._model._rows[r] for r in rows if r < len(self._model._rows)]


class TracePanel(QWidget):
    cleared = Signal()
    request_send = Signal(object)

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.setSpacing(2)

        bar = QHBoxLayout()
        bar.setSpacing(6)

        self.clear_btn = QPushButton(_("Trace.Clear"))
        self.clear_btn.clicked.connect(self._on_clear)
        bar.addWidget(self.clear_btn)

        self.pause_btn = QPushButton(_("Trace.Pause"))
        self.pause_btn.setCheckable(True)
        self.pause_btn.toggled.connect(self._on_pause)
        bar.addWidget(self.pause_btn)

        self.autoscroll_chk = QCheckBox(_("Trace.AutoScroll"))
        self.autoscroll_chk.setChecked(True)
        self.autoscroll_chk.toggled.connect(self._on_autoscroll)
        bar.addWidget(self.autoscroll_chk)

        self.collapse_chk = QCheckBox(_("Trace.Collapse"))
        self.collapse_chk.setToolTip("Show only latest frame per CAN ID")
        
        self.collapse_chk.toggled.connect(self._on_collapse)
        bar.addWidget(self.collapse_chk)


        bar.addStretch()
        layout.addLayout(bar)

        self.view = TraceView(self)
        layout.addWidget(self.view, 1)

        self.summary_label = QLabel(f"{_("Trace.Received")} 0 {_("Trace.Count")}")
        self.summary_label.setStyleSheet(f"color: {FG_DIM};")
        layout.addWidget(self.summary_label)

        self._paused = False
        self._frame_count = 0
        self._log_buffer = deque(maxlen=1000)
        self._last_log_ts = 0.0
        self._summary_timer = QTimer(self)
        self._summary_timer.timeout.connect(self._update_summary)
        self._summary_timer.start(500)

        if getattr(sys, 'frozen', False):
            self._log_path = os.path.join(os.path.dirname(os.path.abspath(sys.executable)), "trace_log.csv")
        else:
            self._log_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "trace_log.csv")
        self._log_timer = QTimer(self)
        self._log_timer.timeout.connect(self._flush_log)
        self._log_timer.start(5000)

    _log_max_rows = 1500

    def _flush_log(self):
        if len(self._log_buffer) == 0:
            return
        try:
            new_frames = []
            for fm in self._log_buffer:
                if fm.is_error or fm.timestamp <= self._last_log_ts:
                    continue
                dt = datetime.fromtimestamp(fm.timestamp)
                ts = dt.strftime("%Y-%m-%d %H:%M:%S") + f".{int(dt.microsecond / 1000):03d}"
                ch = "TX" if fm.is_tx else "CAN1"
                cid = f"{fm.can_id:08X}" if fm.extended else f"{fm.can_id:03X}"
                tp = []
                if fm.fd: tp.append("FD")
                if fm.brs: tp.append("BRS")
                if fm.rtr: tp.append("RTR")
                type_str = " ".join(tp) or ("Ext" if fm.extended else "SFF")
                new_frames.append([ts, ch, cid, type_str, str(fm.dlc), fm.data.hex(" ").upper()])
                if fm.timestamp > self._last_log_ts:
                    self._last_log_ts = fm.timestamp
            if not new_frames:
                return
            need_header = not os.path.isfile(self._log_path) or os.path.getsize(self._log_path) == 0
            with open(self._log_path, "a", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                if need_header:
                    w.writerow(["timestamp", "ch", "can_id", "type", "dlc", "data_hex"])
                for r in new_frames:
                    w.writerow(r)
            # trim to _log_max_rows when file gets large
            try:
                with open(self._log_path, "r", newline="", encoding="utf-8") as f:
                    all_rows = list(csv.reader(f))
                if len(all_rows) > self._log_max_rows:
                    with open(self._log_path, "w", newline="", encoding="utf-8") as f:
                        w = csv.writer(f)
                        w.writerow(all_rows[0])  # header
                        for r in all_rows[-(self._log_max_rows - 1):]:
                            w.writerow(r)
            except Exception:
                pass
        except Exception:
            pass
        finally:
            self._log_buffer.clear()

    def _on_clear(self):
        self.view.clear()
        self._frame_count = 0
        self._log_buffer.clear()
        self._last_log_ts = 0.0
        self.summary_label.setText("Received: 0 frames")
        self.cleared.emit()

    def _on_pause(self, checked):
        self._paused = checked
        self.pause_btn.setText(_("Trace.Resume") if checked else _("Trace.Pause"))

    def _on_autoscroll(self, checked):
        self.view._auto_scroll = checked
        if checked:
            sb = self.view.verticalScrollBar()
            sb.setValue(sb.maximum())

    def _on_collapse(self, checked):
        self.view.set_collapse_mode(checked)
        self._update_summary()

    def _update_summary(self):
        n = self._frame_count
        ids = self.view.id_summary()
        uniq = len(ids)
        mode = _("Trace.ModeCollapsed") if self.collapse_chk.isChecked() else _("Trace.ModeAll")
        self.summary_label.setText(
            f"{_("Trace.Received")} {n} {_("Trace.Frames")}   {_("Trace.UniqueIDs")} {uniq}   [{mode}]"
        )

    def append_frame(self, frame: CANFrame):
        if self._paused:
            return
        self.view.add_frame(frame)
        self._frame_count += 1
        self._log_buffer.append(frame)

    def refresh_language(self):
        self.clear_btn.setText(_("Trace.Clear"))
        self.pause_btn.setText(_("Trace.Resume") if self._paused else _("Trace.Pause"))
        self.autoscroll_chk.setText(_("Trace.AutoScroll"))
        self.collapse_chk.setText(_("Trace.Collapse"))
        self._update_summary()
        self.view._model.update_headers()
        hdr = self.view.horizontalHeader()
        hdr.update()
        self.view.update()
        if hasattr(hdr, "headerDataChanged"):
            hdr.headerDataChanged(Qt.Horizontal, 0, self.view._model.columnCount()-1)

    def clear_all(self):
        self._on_clear()
