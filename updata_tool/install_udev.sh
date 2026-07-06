#!/usr/bin/env bash
# 在 Linux 上无需 sudo 访问 ZDT_CANable / CANable2 / candleLight 的 udev 规则安装脚本
# 用法：sudo bash install_udev.sh

set -e

RULES_FILE=/etc/udev/rules.d/99-zdt-canable.rules

cat > "$RULES_FILE" <<'EOF'
# ZDT_CANable 2.0 PRO / canable2 固件（github.com/normaldotcom/canable2）
# ID_MM_DEVICE_IGNORE: 阻止 ModemManager 把设备当调制解调器探测
#   ModemManager 会在 ttyACM0 上发 AT 命令 → 干扰 slcan 协议 → E=4 bus-off
SUBSYSTEM=="usb", ATTR{idVendor}=="16d0", ATTR{idProduct}=="117e", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
KERNEL=="ttyACM*", ATTRS{idVendor}=="16d0", ATTRS{idProduct}=="117e", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1", ENV{ID_MM_PORT_IGNORE}="1"

# candleLight / ZDT 默认固件
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="606f", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="6069", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="606a", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="606b", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="606c", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="606d", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="606e", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="60ac", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"

# pid.codes gs_usb
SUBSYSTEM=="usb", ATTR{idVendor}=="1209", ATTR{idProduct}=="2323", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
SUBSYSTEM=="usb", ATTR{idVendor}=="1209", ATTR{idProduct}=="2322", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"

# MDFLY 旧版
SUBSYSTEM=="usb", ATTR{idVendor}=="16d0", ATTR{idProduct}=="0fdb", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
EOF

echo "[*] 写入规则: $RULES_FILE"
cat "$RULES_FILE"
echo "[*] 重新加载 udev 规则"
udevadm control --reload-rules
udevadm trigger
echo "[*] 完成。重新插拔设备即可免 root 访问。"
