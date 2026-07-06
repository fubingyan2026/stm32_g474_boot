"""Filter / Statistics 面板。"""
from __future__ import annotations

from typing import List

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
                                QTableWidget, QTableWidgetItem, QHeaderView,
                                QAbstractItemView, QDialog, QDialogButtonBox,
                                QFormLayout, QLineEdit, QSpinBox, QCheckBox,
                                QLabel, QGroupBox, QGridLayout)

from .worker import CANFilter
from .i18n import _
from .style import id_color, FG_DIM, FG_ACCENT


# --------------------------------------------------------------------------- #
#  Filter 编辑对话框
# --------------------------------------------------------------------------- #
class FilterDialog(QDialog):
    def __init__(self, filt: CANFilter, parent=None):
        super().__init__(parent)
        self.setWindowTitle(_("Filter.DlgTitle"))
        form = QFormLayout(self)

        self.min_edit = QLineEdit(f"{filt.can_id_min:X}")
        self.max_edit = QLineEdit(f"{filt.can_id_max:X}")
        form.addRow(_("Filter.DlgIDMin"), self.min_edit)
        form.addRow(_("Filter.DlgIDMax"), self.max_edit)

        self.ext_chk = QCheckBox(_("Filter.DlgExt"))
        self.ext_chk.setChecked(filt.extended)
        form.addRow("", self.ext_chk)

        self.discard_chk = QCheckBox(_("Filter.DlgDiscard"))
        self.discard_chk.setChecked(filt.pass_discard)
        form.addRow("", self.discard_chk)

        btns = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        btns.accepted.connect(self.accept)
        btns.rejected.connect(self.reject)
        form.addRow(btns)

    def get_filter(self) -> CANFilter:
        try:
            mn = int(self.min_edit.text(), 16)
        except ValueError:
            mn = 0
        try:
            mx = int(self.max_edit.text(), 16)
        except ValueError:
            mx = 0x7FF if not self.ext_chk.isChecked() else 0x1FFFFFFF
        return CANFilter(
            can_id_min=mn,
            can_id_max=mx,
            extended=self.ext_chk.isChecked(),
            pass_discard=self.discard_chk.isChecked(),
        )


# --------------------------------------------------------------------------- #
#  过滤面板
# --------------------------------------------------------------------------- #
class FilterPanel(QWidget):
    filters_changed = Signal(list)  # List[CANFilter]

    HEADERS = [_("Filter.HdrIndex"), _("Filter.HdrRange"), _("Filter.HdrType"), _("Filter.HdrAction")]
    COL_IDX, COL_RANGE, COL_TYPE, COL_ACTION = range(4)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.filters: List[CANFilter] = []
        self._init_ui()

    def _init_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)

        self.info_label = QLabel(_("Filter.Info"))
        self.info_label.setStyleSheet(f"color: {FG_DIM};")
        self.info_label.setWordWrap(True)
        layout.addWidget(self.info_label)

        self.table = QTableWidget(0, len(self.HEADERS), self)
        self.table.setHorizontalHeaderLabels([_("Filter.HdrIndex"), _("Filter.HdrRange"), _("Filter.HdrType"), _("Filter.HdrAction")])
        self.table.verticalHeader().setVisible(False)
        self.table.verticalHeader().setDefaultSectionSize(20)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setShowGrid(False)
        h = self.table.horizontalHeader()
        h.resizeSection(self.COL_IDX, 30)
        h.resizeSection(self.COL_TYPE, 70)
        h.resizeSection(self.COL_ACTION, 70)
        h.setSectionResizeMode(self.COL_RANGE, QHeaderView.Stretch)
        layout.addWidget(self.table, 1)

        bar = QHBoxLayout()
        self.add_btn   = QPushButton(_("Filter.Add"))
        self.edit_btn  = QPushButton(_("Filter.Edit"))
        self.del_btn   = QPushButton(_("Filter.Delete"))
        self.clear_btn = QPushButton(_("Filter.Clear"))
        for b in (self.add_btn, self.edit_btn, self.del_btn, self.clear_btn):
            bar.addWidget(b)
        bar.addStretch()
        layout.addLayout(bar)

        self.add_btn.clicked.connect(self._on_add)
        self.edit_btn.clicked.connect(self._on_edit)
        self.del_btn.clicked.connect(self._on_delete)
        self.clear_btn.clicked.connect(self._on_clear)

    def _refresh(self):
        self.table.setRowCount(len(self.filters))
        for i, f in enumerate(self.filters):
            def it(text, align=None, color=None):
                x = QTableWidgetItem(text)
                if align is not None:
                    x.setTextAlignment(align)
                if color is not None:
                    from PySide6.QtGui import QBrush, QColor
                    x.setBackground(QBrush(QColor(color)))
                x.setFlags(x.flags() & ~Qt.ItemIsEditable)
                return x
            self.table.setItem(i, self.COL_IDX, it(f"{i+1}", Qt.AlignCenter))
            self.table.setItem(i, self.COL_RANGE,
                it(f"0x{f.can_id_min:X} - 0x{f.can_id_max:X}"))
            self.table.setItem(i, self.COL_TYPE,
                it("Ext" if f.extended else "Std", Qt.AlignCenter))
            self.table.setItem(i, self.COL_ACTION,
                it(_("Filter.Drop") if f.pass_discard else _("Filter.Pass"), Qt.AlignCenter))

    def _selected_index(self):
        rows = self.table.selectionModel().selectedRows()
        return rows[0].row() if rows else None

    def _emit(self):
        self.filters_changed.emit(list(self.filters))

    def _on_add(self):
        dlg = FilterDialog(CANFilter(0, 0x7FF), self)
        if dlg.exec() == QDialog.Accepted:
            self.filters.append(dlg.get_filter())
            self._refresh()
            self._emit()

    def _on_edit(self):
        i = self._selected_index()
        if i is None: return
        dlg = FilterDialog(self.filters[i], self)
        if dlg.exec() == QDialog.Accepted:
            self.filters[i] = dlg.get_filter()
            self._refresh()
            self._emit()

    def _on_delete(self):
        i = self._selected_index()
        if i is None: return
        self.filters.pop(i)
        self._refresh()
        self._emit()

    def _on_clear(self):
        self.filters.clear()
        self._refresh()
        self._emit()

    def refresh_language(self):
        self.add_btn.setText(_("Filter.Add"))
        self.edit_btn.setText(_("Filter.Edit"))
        self.del_btn.setText(_("Filter.Delete"))
        self.clear_btn.setText(_("Filter.Clear"))
        self.info_label.setText(_("Filter.Info"))
        self.table.setHorizontalHeaderLabels([_("Filter.HdrIndex"), _("Filter.HdrRange"), _("Filter.HdrType"), _("Filter.HdrAction")])
        self._refresh()

    def to_dict_list(self):
        return [
            {"can_id_min": f.can_id_min, "can_id_max": f.can_id_max,
             "extended": f.extended, "pass_discard": f.pass_discard}
            for f in self.filters
        ]

    def from_dict_list(self, lst):
        self.filters = [CANFilter(**d) for d in lst]
        self._refresh()
        self._emit()


# --------------------------------------------------------------------------- #
#  Statistics 面板
# --------------------------------------------------------------------------- #
class StatisticsPanel(QWidget):
    """按 CAN ID 显示：帧数、最新时间、周期、负载占比。"""

    def refresh_language(self):
        self.table.setHorizontalHeaderLabels([_("Stat.HdrID"), _("Stat.HdrType"), _("Stat.HdrCount"), _("Stat.HdrPeriod"), _("Stat.HdrLastDelta")])

    def __init__(self, parent=None):
        super().__init__(parent)
        self._last_summary = []
        self._last_load = 0.0
        self._last_fps = 0
        self._init_ui()
        self._total = 0

    def _init_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)

        # 顶部统计
        top = QGroupBox(_("Stat.BusStatus"))
        g = QGridLayout(top)
        self.lbl_total = QLabel("0")
        self.lbl_fps   = QLabel("0")
        self.lbl_load  = QLabel("0%")
        self.lbl_unique = QLabel("0")

        for lbl in (self.lbl_total, self.lbl_fps, self.lbl_unique):
            lbl.setStyleSheet(f"color: {FG_ACCENT}; font-size: 18pt; font-weight: bold;")
        self.lbl_load.setStyleSheet(f"color: {FG_ACCENT}; font-size: 18pt; font-weight: bold;")

        g.addWidget(QLabel(_("Stat.TotalFrames")), 0, 0); g.addWidget(self.lbl_total, 0, 1)
        g.addWidget(QLabel(_("Stat.FPS")), 0, 2); g.addWidget(self.lbl_fps, 0, 3)
        g.addWidget(QLabel(_("Stat.BusLoad")), 1, 0); g.addWidget(self.lbl_load, 1, 1)
        g.addWidget(QLabel(_("Stat.UniqueID")), 1, 2); g.addWidget(self.lbl_unique, 1, 3)
        layout.addWidget(top)

        # ID 详细统计
        detail_label = QLabel(_("Stat.IDDetail"))
        detail_label.setStyleSheet(f"color: {FG_ACCENT}; font-weight: bold; padding-top: 6px;")
        layout.addWidget(detail_label)

        self.table = QTableWidget(0, 5, self)
        self.table.setHorizontalHeaderLabels([_("Stat.HdrID"), _("Stat.HdrType"), _("Stat.HdrCount"), _("Stat.HdrPeriod"), _("Stat.HdrLastDelta")])
        self.table.verticalHeader().setVisible(False)
        self.table.verticalHeader().setDefaultSectionSize(20)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        h = self.table.horizontalHeader()
        h.resizeSection(0, 110); h.resizeSection(1, 60)
        h.resizeSection(2, 80); h.resizeSection(3, 110)
        h.setSectionResizeMode(4, QHeaderView.Stretch)
        layout.addWidget(self.table, 1)

    def update_stats(self, load: float, fps: int, total: int, summary: list):
        self._last_load = load
        self._last_fps  = fps
        self._last_summary = summary
        self._total = total

        self.lbl_total.setText(f"{total}")
        self.lbl_fps.setText(f"{fps}")
        self.lbl_unique.setText(f"{len(summary)}")
        self.lbl_load.setText(f"{load:.1f}%")
        if load < 40:
            self.lbl_load.setStyleSheet("color: #4ec9b0; font-size: 18pt; font-weight: bold;")
        elif load < 75:
            self.lbl_load.setStyleSheet("color: #d7ba7d; font-size: 18pt; font-weight: bold;")
        else:
            self.lbl_load.setStyleSheet("color: #f48771; font-size: 18pt; font-weight: bold;")

        # 表格
        self.table.setRowCount(len(summary))
        from PySide6.QtGui import QBrush, QColor
        for i, (can_id, ext, count, period) in enumerate(summary):
            def it(text, align=None, color=None):
                x = QTableWidgetItem(text)
                if align is not None:
                    x.setTextAlignment(align)
                if color is not None:
                    x.setBackground(QBrush(QColor(color)))
                x.setFlags(x.flags() & ~Qt.ItemIsEditable)
                return x
            self.table.setItem(i, 0, it(f"{can_id:08X}" if ext else f"{can_id:03X}",
                                         color=id_color(can_id, ext)))
            self.table.setItem(i, 1, it("Ext" if ext else "Std", Qt.AlignCenter))
            self.table.setItem(i, 2, it(str(count), Qt.AlignCenter))
            self.table.setItem(i, 3, it(f"{period:.1f}" if period else "-",
                                         Qt.AlignCenter))
            self.table.setItem(i, 4, it(""))
