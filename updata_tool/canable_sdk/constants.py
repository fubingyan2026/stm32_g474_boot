"""USB-CAN device identifiers, protocol constants, and flag enums.

CANable 2.5 — ElmueSoft Candlelight protocol.
"""
from __future__ import annotations

import logging

logger = logging.getLogger("canable_sdk")

# USB identifiers
CANABLE_VID = 0x1D50
CANABLE_PID = 0x606F
EP_IN  = 0x81
EP_OUT = 0x02
MAX_PACKET_SIZE = 64

# --- ElmueSoft message types ---
MSG_TxFrame  = 10
MSG_TxEcho   = 11
MSG_RxFrame  = 12
MSG_Error    = 13
MSG_String   = 14
MSG_Busload  = 15

VALID_MSG_TYPES = {MSG_TxFrame, MSG_TxEcho, MSG_RxFrame, MSG_Error, MSG_String, MSG_Busload}
MAX_ELMUE_MSG_SIZE = 128

# --- Control request codes (eUsbRequest) ---
GS_ReqSetHostFormat     = 0
GS_ReqSetBitTiming      = 1
GS_ReqSetDeviceMode     = 2
GS_ReqGetCapabilities   = 4
GS_ReqGetDeviceVersion  = 5
GS_ReqGetTimestamp      = 6
GS_ReqIdentify          = 7
GS_ReqSetBitTimingFD    = 10
GS_ReqGetCapabilitiesFD = 11
GS_ReqSetTermination    = 12
GS_ReqGetTermination    = 13
ELM_ReqGetBoardInfo     = 20
ELM_ReqSetFilter        = 21
ELM_ReqGetLastError     = 22
ELM_ReqSetBusLoadReport = 23

# --- Device flags (eDeviceFlags) ---
GS_DevFlagListenOnly       = 0x0001
GS_DevFlagLoopback         = 0x0002
GS_DevFlagOneShot          = 0x0008
GS_DevFlagTimestamp        = 0x0010
GS_DevFlagIdentify         = 0x0020
GS_DevFlagCAN_FD           = 0x0100
GS_DevFlagBitTimingFD      = 0x0400
GS_DevFlagTermination      = 0x0800
ELM_DevFlagProtocolElmue   = 0x4000
ELM_DevFlagDisableTxEcho   = 0x8000

# --- CAN ID flags (eCanIdFlags) ---
CAN_ID_Error  = 0x20000000
CAN_ID_RTR    = 0x40000000
CAN_ID_29Bit  = 0x80000000
CAN_MASK_11   = 0x000007FF
CAN_MASK_29   = 0x1FFFFFFF

# --- Frame flags (eFrameFlags) ---
FRM_FDF = 0x02
FRM_BRS = 0x04
FRM_ESI = 0x08

# --- Error flags (eErrFlagsCanID) ---
ERID_Tx_Timeout         = 0x0001
ERID_Arbitration_lost   = 0x0002
ERID_Controller_problem = 0x0004
ERID_Protocol_violation = 0x0008
ERID_Transceiver_error  = 0x0010
ERID_No_ACK_received    = 0x0020
ERID_Bus_is_off         = 0x0040
ERID_Bus_error          = 0x0080
ERID_Controller_restarted = 0x0100
ERID_CRC_Error          = 0x0200

# --- Error app flags (eErrorAppFlags, err_data[5]) ---
APP_CanRxFail      = 0x01
APP_CanTxFail      = 0x02
APP_CanTxOverflow  = 0x04
APP_UsbInOverflow  = 0x08
APP_CanTxTimeout   = 0x10

# --- Error bus status (eErrorBusStatus, err_data[1] high nibble) ---
BUS_StatusActive  = 0x00
BUS_StatusWarning = 0x10
BUS_StatusPassive = 0x20
BUS_StatusOff     = 0x30

# --- Error byte 1 flags (eErrFlagsByte1) ---
ER1_Rx_Errors_at_warning_level = 0x04
ER1_Tx_Errors_at_warning_level = 0x08
ER1_Rx_Passive_status_reached  = 0x10
ER1_Tx_Passive_status_reached  = 0x20
ER1_Bus_is_back_active         = 0x40

# --- Misc ---
HOST_FORMAT_MAGIC = 0x0000BEEF
LEGACY_FRAME_SIZE = 80

# --- CAN FD DLC mapping ---
CAN_FD_DLC_MAP = {
    0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8,
    9: 12, 10: 16, 11: 20, 12: 24, 13: 32, 14: 48, 15: 64,
}

DLC_BOUNDARIES = [8, 12, 16, 20, 24, 32, 48, 64]
