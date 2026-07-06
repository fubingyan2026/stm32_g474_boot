"""固件升级上位机启动脚本 — 双击运行或 python flash_gui.py"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from updata_tool.flash_tool.__main__ import main

main()
