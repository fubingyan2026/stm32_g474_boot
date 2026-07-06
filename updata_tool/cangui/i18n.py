"""Chinese / English translation module."""

from __future__ import annotations

from PySide6.QtCore import QObject, Signal

class _Signal(QObject):
    lang_changed = Signal(str)

_signal = _Signal()
language_changed = _signal.lang_changed

_lang = "zh" 

_TR: dict[str, dict[str, str]] = {}


def _(key: str) -> str:
    entry = _TR.get(key)
    return entry.get(_lang, key) if entry else key


def set_language(lang: str):
    global _lang
    _lang = lang
    language_changed.emit(lang)


def get_language() -> str:
    return _lang


# ── register translations ────────────────────────────────────────────
_TR.update({
    # Menus
    "Menu.File":             {"zh": "文件(&F)",  "en": "&File"},
    "Menu.Windows":          {"zh": "窗口(&W)",  "en": "&Windows"},
    "Menu.Hardware":         {"zh": "硬件(&H)",  "en": "&Hardware"},
    "Menu.Tools":            {"zh": "工具(&T)",  "en": "&Tools"},
    "Menu.Help":             {"zh": "帮助(&H)",  "en": "&Help"},
    "Menu.Language":         {"zh": "语言",      "en": "Language"},
    "Menu.Theme":           {"zh": "主题",      "en": "Theme"},

    # File menu items
    "File.OpenTrace":        {"zh": "打开 Trace(&O)…", "en": "&Open Trace…"},
    "File.SaveTrace":        {"zh": "保存 Trace(&S)…", "en": "&Save Trace…"},
    "File.SaveSendList":     {"zh": "保存发送列表…",    "en": "Save Send List…"},
    "File.LoadSendList":     {"zh": "加载发送列表…",    "en": "Load Send List…"},
    "File.Exit":             {"zh": "退出(&X)", "en": "E&xit"},

    # View / Windows menu items
    "Window.SendMessages":   {"zh": "发送消息", "en": "Send Messages"},
    "Window.Filters":        {"zh": "过滤器",  "en": "Filters"},

    # Hardware menu items
    "HW.ScanDevices":        {"zh": "扫描设备",     "en": "Scan Devices"},
    "HW.LEDIdentify":        {"zh": "LED 闪烁识别", "en": "LED Identify"},
    "HW.SilentMode":         {"zh": "Silent 模式 (只听不发)", "en": "Silent Mode (Listen Only)"},

    # Tools menu items
    "Tools.QuickSend":       {"zh": "快速发送…", "en": "Quick Send…"},

    # Help menu items
    "Help.About":            {"zh": "关于 CANable 2.5", "en": "About CANable 2.5"},

    # Language names
    "Lang.Chinese":          {"zh": "中文",   "en": "Chinese"},
    "Lang.English":          {"zh": "英文",   "en": "English"},
    "Theme.Light":          {"zh": "浅色",     "en": "Light"},
    "Theme.Dark":           {"zh": "深色",     "en": "Dark"},

    # Trace column headers
    "Trace.No":              {"zh": "序号",        "en": "No."},
    "Trace.Time":            {"zh": "时间(s)",     "en": "Time (s)"},
    "Trace.Ch":              {"zh": "通道",        "en": "Ch"},
    "Trace.ID":              {"zh": "ID",          "en": "ID"},
    "Trace.Type":            {"zh": "类型",        "en": "Type"},
    "Trace.DLC":             {"zh": "DLC",         "en": "DLC"},
    "Trace.Data":            {"zh": "数据(hex)",   "en": "Data (hex)"},
    "Trace.ASCII":           {"zh": "ASCII",       "en": "ASCII"},
    "Trace.Delta":           {"zh": "间隔(ms)",     "en": "dt (ms)"},
    "Trace.Period":          {"zh": "周期(ms)",    "en": "Period (ms)"},
    "Trace.Count":           {"zh": "计数",        "en": "Count"},

    # Trace toolbar
    "Trace.Clear":           {"zh": "清空",        "en": "Clear"},
    "Trace.Pause":           {"zh": "暂停",        "en": "Pause"},
    "Trace.Resume":          {"zh": "继续",        "en": "Resume"},
    "Trace.AutoScroll":      {"zh": "自动滚动",    "en": "Auto scroll"},
    "Trace.Collapse":        {"zh": "折叠 ID",     "en": "Collapse"},
    "Trace.MaxRows":         {"zh": "最大行数:",   "en": "Max rows:"},
    "Trace.Received":        {"zh": "已接收:",     "en": "Received:"},
    "Trace.UniqueIDs":       {"zh": "唯一 ID:",    "en": "Unique IDs:"},

    # Send panel
    "Send.Add":              {"zh": "添加",        "en": "Add"},
    "Send.Edit":             {"zh": "编辑",        "en": "Edit"},
    "Send.Delete":           {"zh": "删除",        "en": "Delete"},
    "Send.SendOnce":         {"zh": "发送一次",    "en": "Send once"},
    "Send.StartAll":         {"zh": "启动全部",    "en": "Start all"},
    "Send.StopAll":          {"zh": "停止全部",    "en": "Stop all"},
    "Send.ClearAll":         {"zh": "清空列表",    "en": "Clear list"},
    "Send.DialogTitle":      {"zh": "编辑发送报文", "en": "Edit Send Message"},
    "Send.Ready":            {"zh": "就绪",        "en": "Ready"},

    # Send panel table headers
    "Send.HdrName":        {"zh": "名称",    "en": "Name"},
    "Send.HdrIndex":         {"zh": "#",       "en": "#"},
    "Send.HdrID":            {"zh": "ID",      "en": "ID"},
    "Send.HdrType":          {"zh": "类型",    "en": "Type"},
    "Send.HdrDLC":           {"zh": "DLC",     "en": "DLC"},
    "Send.HdrData":          {"zh": "数据",    "en": "Data"},
    "Send.HdrPeriod":        {"zh": "周期",    "en": "Period"},
    "Send.HdrSent":          {"zh": "已发",    "en": "Sent"},
    "Send.HdrOn":            {"zh": "开启",    "en": "On"},

    # Send dialog form labels
    "Send.DlgID":            {"zh": "CAN ID (hex):",   "en": "CAN ID (hex):"},
    "Send.DlgName":        {"zh": "名称:",           "en": "Name:"},
    "Send.DlgType":          {"zh": "类型:",           "en": "Type:"},
    "Send.DlgExt":           {"zh": "扩展帧 (29-bit)",  "en": "Extended (29-bit)"},
    "Send.DlgRTR":           {"zh": "RTR 远程帧",      "en": "RTR"},
    "Send.DlgFD":            {"zh": "CAN FD",          "en": "CAN FD"},
    "Send.DlgBRS":           {"zh": "BRS",             "en": "BRS"},
    "Send.DlgDLC":           {"zh": "DLC:",            "en": "DLC:"},
    "Send.DlgData":          {"zh": "数据:",            "en": "Data:"},
    "Send.DlgPeriod":        {"zh": "周期(ms):",       "en": "Period (ms):"},
    "Send.DlgEnabled":       {"zh": "启用周期发送",    "en": "Enable Periodic"},
    "Send.FdDlcHEX":         {"zh": "十六进制，空格分隔", "en": "Hex, space separated"},

    # Filter panel
    "Filter.Title":          {"zh": "过滤器",       "en": "Filters"},
    "Filter.Add":            {"zh": "添加过滤",     "en": "Add filter"},
    "Filter.Delete":         {"zh": "删除过滤",     "en": "Delete filter"},
    "Filter.IDType":         {"zh": "ID 类型:",    "en": "ID type:"},
    "Filter.Action":         {"zh": "动作:",        "en": "Action:"},

    # Left panel
    "Left.Bus":              {"zh": "总线",            "en": "Bus"},
    "Left.Bitrate":          {"zh": "比特率:",         "en": "Bitrate:"},
    "Left.DataBitrate":      {"zh": "数据比特率:",     "en": "Data Bitrate:"},
    "Left.SamplePoint":      {"zh": "采样点:",         "en": "Sample Point:"},
    "Left.Devices":          {"zh": "设备",            "en": "Devices"},
    "Left.Scan":             {"zh": "扫描设备",        "en": "Scan"},
    "Left.QuickActions":     {"zh": "快速操作",        "en": "Quick Actions"},
    "Left.Connect":          {"zh": "连接",            "en": "Connect"},
    "Left.Disconnect":       {"zh": "断开",            "en": "Disconnect"},
    "Left.LEDIdentify":      {"zh": "LED 闪烁识别",    "en": "LED Identify"},
    "Left.StatusCardTitle":  {"zh": "CAN 总线状态",    "en": "CAN Bus Status"},
    "Left.NotConnected":     {"zh": "未连接",          "en": "Not Connected"},

    # Status bar
    "Status.Connected":      {"zh": "已连接",          "en": "Connected"},
    "Status.Disconnected":   {"zh": "未连接",          "en": "Not connected"},
    "Status.FPS":            {"zh": "帧/秒",           "en": "fps"},
    "Status.Load":           {"zh": "负载",            "en": "Load"},
    "Status.TotalFrames":    {"zh": "总帧数",          "en": "Total frames"},
    "Status.NoAck":          {"zh": "NO-ACK — 无设备应答", "en": "NO-ACK — no responding nodes"},

    # Error messages
    "Trace.Frames":        {"zh": "帧",           "en": "frames"},
    "Trace.ModeAll":       {"zh": "全部",         "en": "all"},
    "Trace.ModeCollapsed": {"zh": "折叠",         "en": "collapsed"},
    "Error.CONotFound":      {"zh": "未找到 CANable 设备",  "en": "CANable device not found"},
    "Error.ConfigSave":      {"zh": "配置保存失败",          "en": "Failed to save settings"},
    "About.Title":          {"zh": "关于 CANable 2.5", "en": "About CANable 2.5"},
    "About.Desc":           {"zh": "CANable 2.5 USB-CAN 适配器 上位机", "en": "CANable 2.5 USB-CAN Adapter GUI"},
    "About.Tech":           {"zh": "基于 PySide6 / Qt 6 + canable_sdk 驱动", "en": "Built with PySide6 / Qt 6 + canable_sdk driver"},
    "Error.ConfigLoad":      {"zh": "配置加载失败",          "en": "Failed to load settings"},
    # Filter panel
    "Filter.Info":          {"zh": "过滤器：未命中任何规则时默认放行。", "en": "Filters: unmatched frames pass by default."},
    "Filter.ActionDesc":    {"zh": "丢弃/放行",   "en": "Drop/Pass"},
    "Filter.Drop":          {"zh": "丢弃",        "en": "Drop"},
    "Filter.Pass":          {"zh": "放行",        "en": "Pass"},
    "Filter.Add":           {"zh": "添加",        "en": "Add"},
    "Filter.Edit":          {"zh": "编辑",        "en": "Edit"},
    "Filter.Delete":        {"zh": "删除",        "en": "Delete"},
    "Filter.Clear":         {"zh": "清空",        "en": "Clear"},
    "Filter.HdrIndex":      {"zh": "#",          "en": "#"},
    "Filter.HdrRange":      {"zh": "ID 范围",    "en": "ID Range"},
    "Filter.HdrType":       {"zh": "类型",       "en": "Type"},
    "Filter.HdrAction":     {"zh": "动作",       "en": "Action"},
    "Filter.DlgTitle":      {"zh": "编辑过滤规则","en": "Edit Filter Rule"},
    "Filter.DlgIDMin":      {"zh": "CAN ID 起始 (hex):", "en": "CAN ID Min (hex):"},
    "Filter.DlgIDMax":      {"zh": "CAN ID 结束 (hex):", "en": "CAN ID Max (hex):"},
    "Filter.DlgExt":        {"zh": "仅作用于扩展帧 (29-bit)", "en": "Extended only (29-bit)"},
    "Filter.DlgDiscard":    {"zh": "丢弃区间内的帧", "en": "Discard matching frames"},
    # Statistics panel
    "Stat.BusStatus":       {"zh": "总线状态",    "en": "Bus Status"},
    "Stat.TotalFrames":     {"zh": "总帧数",      "en": "Total"},
    "Stat.FPS":             {"zh": "帧率",        "en": "FPS"},
    "Stat.BusLoad":         {"zh": "总线负载",    "en": "Bus Load"},
    "Stat.UniqueID":        {"zh": "唯一 ID",     "en": "Unique IDs"},
    "Stat.IDDetail":        {"zh": "ID 详细",     "en": "ID Details"},
    "Stat.HdrID":           {"zh": "ID",          "en": "ID"},
    "Stat.HdrType":         {"zh": "类型",        "en": "Type"},
    "Stat.HdrCount":        {"zh": "计数",        "en": "Count"},
    "Stat.HdrPeriod":       {"zh": "周期(ms)",    "en": "Period (ms)"},
    "Stat.HdrLastDelta":    {"zh": "最近间隔(ms)","en": "Last Δt (ms)"},
    # Main window misc
    "Scan.Failed":          {"zh": "扫描失败",    "en": "Scan failed"},
    "LED.Identify.Fail":    {"zh": "LED 识别失败","en": "LED Identify failed"},
    "SilentMode.Fail":      {"zh": "Silent 模式切换失败", "en": "Silent mode toggle failed"},
    "Load.Failed":          {"zh": "加载失败",    "en": "Load failed"},
    "Format.Error":         {"zh": "格式错误",    "en": "Format error"},

})
