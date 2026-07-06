"""CANable 2.5 theme -- macaron light / dark"""

import os

_HERE = os.path.dirname(os.path.abspath(__file__))

# ---- palettes ----
_LIGHT = {
    "BG_MAIN": "#F5F0E8", "BG_CARD": "#FFFFFF", "BG_INPUT": "#FDFCFA",
    "BG_HEADER": "#EDE6DA", "BG_HOVER": "#D2F0E3", "BG_SELECT": "#BAE6D3",
    "BG_ACCENT": "#7EC8A0", "BG_CORAL": "#F2A999", "BG_CORAL_H": "#ED9481",
    "BG_SIDEBAR": "#F8F4EC", "BG_STATUS": "#D8F0E2",
    "FG_TEXT": "#3A3A3A", "FG_DIM": "#9E9E9E", "FG_ACCENT": "#4C9B73",
    "FG_CORAL": "#D4745C", "FG_WARN": "#D4A24C", "FG_ERROR": "#D4655C",
    "FG_LINK": "#5B9BD5",
    "BORDER": "#E4DED4",
    "LOAD_LOW": "#4C9B73", "LOAD_MID": "#D4A24C", "LOAD_HIGH": "#D4655C",
}

_DARK = {
    "BG_MAIN": "#2B2B2B", "BG_CARD": "#333333", "BG_INPUT": "#252525",
    "BG_HEADER": "#3A3A3A", "BG_HOVER": "#2A4035", "BG_SELECT": "#2A4A3A",
    "BG_ACCENT": "#7EC8A0", "BG_CORAL": "#F2A999", "BG_CORAL_H": "#ED9481",
    "BG_SIDEBAR": "#2E2E2E", "BG_STATUS": "#253530",
    "FG_TEXT": "#DDDDDD", "FG_DIM": "#888888", "FG_ACCENT": "#6ED8A0",
    "FG_CORAL": "#D4745C", "FG_WARN": "#D4A24C", "FG_ERROR": "#D4655C",
    "FG_LINK": "#5B9BD5",
    "BORDER": "#444444",
    "LOAD_LOW": "#6ED8A0", "LOAD_MID": "#D4A24C", "LOAD_HIGH": "#D4655C",
}

_current_theme = "light"
_p = _LIGHT.copy()

# dynamically updated exports for other modules
BG_MAIN = _p["BG_MAIN"]
BG_CARD = _p["BG_CARD"]
BG_INPUT = _p["BG_INPUT"]
BG_HEADER = _p["BG_HEADER"]
BG_HOVER = _p["BG_HOVER"]
BG_SELECT = _p["BG_SELECT"]
BG_ACCENT = _p["BG_ACCENT"]
BG_CORAL = _p["BG_CORAL"]
BG_CORAL_H = _p["BG_CORAL_H"]
BG_SIDEBAR = _p["BG_SIDEBAR"]
BG_STATUS = _p["BG_STATUS"]
FG_TEXT = _p["FG_TEXT"]
FG_DIM = _p["FG_DIM"]
FG_ACCENT = _p["FG_ACCENT"]
FG_CORAL = _p["FG_CORAL"]
FG_WARN = _p["FG_WARN"]
FG_ERROR = _p["FG_ERROR"]
FG_LINK = _p["FG_LINK"]
BORDER = _p["BORDER"]
LOAD_LOW = _p["LOAD_LOW"]
LOAD_MID = _p["LOAD_MID"]
LOAD_HIGH = _p["LOAD_HIGH"]


def id_color(can_id: int, extended: bool = False) -> str:
    if extended:
        hue = ((can_id >> 16) ^ (can_id & 0xFFFF)) % 360
    else:
        hue = (can_id * 7) % 360
    return f"hsl({hue}, 50%, 60%)"


def _make_qss(p: dict) -> str:
    return f"""
QWidget {{
    background-color: {p['BG_MAIN']};
    color: {p['FG_TEXT']};
    font-family: "Segoe UI", "Noto Sans CJK SC", "Helvetica Neue", "PingFang SC", "Microsoft YaHei", "Noto Sans", sans-serif;
    font-size: 9.5pt;
}}

QMainWindow, QDialog {{
    background-color: {p['BG_MAIN']};
}}

QMenuBar {{
    background-color: {p['BG_CARD']};
    border-bottom: 1px solid {p['BORDER']};
    padding: 2px 4px;
}}

QMenuBar::item:selected {{
    background-color: {p['BG_HOVER']};
    border-radius: 4px;
}}

QMenu {{
    background-color: {p['BG_CARD']};
    border: 1px solid {p['BORDER']};
    border-radius: 6px;
    padding: 4px;
}}

QMenu::item {{
    padding: 5px 24px;
    border-radius: 4px;
}}

QMenu::item:selected {{
    background-color: {p['BG_HOVER']};
}}

QToolBar {{
    background-color: {p['BG_HEADER']};
    border-bottom: 1px solid {p['BORDER']};
    spacing: 6px;
    padding: 5px 6px;
}}

QStatusBar {{
    background-color: {p['BG_HEADER']};
    border-top: 1px solid {p['BORDER']};
    color: {p['FG_DIM']};
    font-size: 9pt;
}}

QTabWidget::pane {{
    border: 1px solid {p['BORDER']};
    border-radius: 6px;
    background-color: {p['BG_CARD']};
}}

QTabBar::tab {{
    background-color: {p['BG_HEADER']};
    color: {p['FG_DIM']};
    padding: 7px 16px;
    border: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    margin-right: 2px;
}}

QTabBar::tab:selected {{
    background-color: {p['BG_CARD']};
    color: {p['FG_ACCENT']};
    font-weight: bold;
}}

QTabBar::tab:hover {{
    background-color: {p['BG_HOVER']};
}}

QPushButton {{
    background-color: {p['BG_CARD']};
    color: {p['FG_TEXT']};
    border: 1px solid {p['BORDER']};
    padding: 5px 14px;
    border-radius: 6px;
    min-height: 28px;
}}

QPushButton:hover {{
    background-color: {p['BG_HOVER']};
    border-color: {p['BG_ACCENT']};
}}

QPushButton:pressed {{
    background-color: {p['BG_SELECT']};
}}

QPushButton:disabled {{
    color: {p['FG_DIM']};
    background-color: {p['BG_HEADER']};
}}

QPushButton#connectBtn {{
    font-weight: bold;
    font-size: 10pt;
    padding: 6px 20px;
    border-radius: 8px;
}}

QPushButton#connectBtn:checked {{
    background-color: {p['BG_ACCENT']};
    color: white;
    border-color: {p['FG_ACCENT']};
}}

QPushButton#sendBtn {{
    background-color: {p['BG_ACCENT']};
    color: white;
    font-weight: bold;
    border: none;
    border-radius: 6px;
    padding: 5px 14px;
}}

QPushButton#sendBtn:hover {{
    background-color: {p['FG_ACCENT']};
}}

QPushButton#actionBtn {{
    background-color: {p['BG_CORAL']};
    color: white;
    font-weight: bold;
    border: none;
    border-radius: 6px;
    padding: 5px 14px;
}}

QPushButton#actionBtn:hover {{
    background-color: {p['BG_CORAL_H']};
}}

QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {{
    background-color: {p['BG_INPUT']};
    color: {p['FG_TEXT']};
    border: 1px solid {p['BORDER']};
    padding: 5px 8px;
    border-radius: 6px;
    selection-background-color: {p['BG_HOVER']};
}}

QComboBox:hover, QLineEdit:hover {{
    border-color: {p['BG_ACCENT']};
}}

QComboBox::drop-down {{
    border-left: 1px solid {p['BORDER']};
    width: 20px;
    border-top-right-radius: 6px;
    border-bottom-right-radius: 6px;
}}

QComboBox QAbstractItemView {{
    background-color: {p['BG_CARD']};
    border: 1px solid {p['BORDER']};
    border-radius: 6px;
    selection-background-color: {p['BG_HOVER']};
    outline: none;
}}

QHeaderView::section {{
    background-color: {p['BG_HEADER']};
    color: {p['FG_TEXT']};
    padding: 6px 8px;
    border: none;
    border-right: 1px solid {p['BORDER']};
    border-bottom: 1px solid {p['BORDER']};
    font-weight: bold;
    font-size: 9pt;
}}

QTableView, QTableWidget {{
    background-color: {p['BG_CARD']};
    color: {p['FG_TEXT']};
    gridline-color: transparent;
    selection-background-color: {p['BG_HOVER']};
    selection-color: {p['FG_TEXT']};
    border: 1px solid {p['BORDER']};
    border-radius: 6px;
}}

QTableView::item, QTableWidget::item {{
    background-color: transparent;
    padding: 3px 6px;
    border-bottom: 1px solid {p['BORDER']};
}}

QTableView::item:selected, QTableWidget::item:selected {{
    background-color: {p['BG_SELECT']};
    color: {p['FG_TEXT']};
}}

QTableView::item:alternate, QTableWidget::item:alternate {{
    background-color: {p['BG_INPUT']};
}}

QTreeView, QListWidget {{
    background-color: {p['BG_CARD']};
    color: {p['FG_TEXT']};
    border: 1px solid {p['BORDER']};
    border-radius: 6px;
    selection-background-color: {p['BG_HOVER']};
    outline: none;
}}

QTreeView::item, QListWidget::item {{
    padding: 3px 6px;
    border-bottom: 1px solid {p['BORDER']};
}}

QTreeView::item:selected, QListWidget::item:selected {{
    background-color: {p['BG_SELECT']};
    color: {p['FG_TEXT']};
}}

QScrollBar:vertical, QScrollBar:horizontal {{
    background-color: {p['BG_MAIN']};
    border: none;
    width: 10px;
    height: 10px;
}}

QScrollBar::handle {{
    background-color: {p['BORDER']};
    border-radius: 5px;
    min-height: 24px;
    min-width: 24px;
}}

QScrollBar::handle:hover {{
    background-color: {p['FG_DIM']};
}}

QScrollBar::add-line, QScrollBar::sub-line {{
    height: 0; width: 0;
}}

QGroupBox {{
    border: 1px solid {p['BORDER']};
    border-radius: 8px;
    margin-top: 14px;
    padding: 12px 8px 8px 8px;
    font-weight: bold;
    background-color: {p['BG_CARD']};
}}

QGroupBox::title {{
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px;
    padding: 0 6px;
    color: {p['FG_ACCENT']};
    font-size: 10pt;
}}

QCheckBox {{
    spacing: 6px;
    color: {p['FG_TEXT']};
}}

QSplitter::handle {{
    background-color: {p['BORDER']};
}}

QSplitter::handle:horizontal {{ width: 1px; }}
QSplitter::handle:vertical {{ height: 1px; }}

QLabel#statusLabel[connected="true"] {{
    color: {p['FG_ACCENT']};
    font-weight: bold;
}}

QLabel#statusLabel[connected="false"] {{
    color: {p['FG_ERROR']};
    font-weight: bold;
}}

QLabel#busLoad {{
    color: {p['LOAD_LOW']};
    font-weight: bold;
    font-size: 11pt;
}}

QLabel#busLoad[level="mid"] {{ color: {p['LOAD_MID']}; }}
QLabel#busLoad[level="high"] {{ color: {p['LOAD_HIGH']}; }}

QFrame#sidebar {{
    background-color: {p['BG_SIDEBAR']};
    border-right: 1px solid {p['BORDER']};
}}

QFrame#statusCard {{
    background-color: {p['BG_STATUS']};
    border: 1px solid {p['BG_ACCENT']};
    border-radius: 10px;
    padding: 10px;
}}

QFrame#quickActions {{
    background-color: {p['BG_CARD']};
    border: 1px solid {p['BORDER']};
    border-radius: 10px;
    padding: 8px;
}}

QToolTip {{
    background-color: {p['BG_CARD']};
    color: {p['FG_TEXT']};
    border: 1px solid {p['BORDER']};
    padding: 4px 8px;
    border-radius: 4px;
    font-size: 9pt;
}}
"""


def _update_globals(pal: dict):
    global BG_MAIN, BG_CARD, BG_INPUT, BG_HEADER, BG_HOVER, BG_SELECT, BG_ACCENT
    global BG_CORAL, BG_CORAL_H, BG_SIDEBAR, BG_STATUS
    global FG_TEXT, FG_DIM, FG_ACCENT, FG_CORAL, FG_WARN, FG_ERROR, FG_LINK, BORDER
    global LOAD_LOW, LOAD_MID, LOAD_HIGH
    global _p
    _p = pal.copy()
    for k, v in pal.items():
        globals()[k] = v


def get_qss() -> str:
    return _make_qss(_p)


def set_theme(name: str):
    global _current_theme
    pal = _DARK if name == "dark" else _LIGHT
    _current_theme = name
    _update_globals(pal)


def current_theme() -> str:
    return _current_theme


# initial QSS
QSS = get_qss()
