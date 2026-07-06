/**
 * @file    drv_can.c
 * @author  maximillian
 * @version V1.3.0
 * @date    2026-07-6
 * @brief   CAN 设备驱动实现（FDCAN 经典/CAN FD，中断接收，多通道）
 */

/* Includes ------------------------------------------------------------------*/
#include "drv_can.h"

#include "main.h"

#include <string.h>

/* Private types -------------------------------------------------------------*/

typedef struct {
    FDCAN_HandleTypeDef*  hfdcan;
    drv_can_rx_callback_t rx_callback;
    bool                  initialized;
} drv_can_ctx_t;

/* Private variables ---------------------------------------------------------*/

static drv_can_ctx_t s_ctx[DRV_CAN_CH_NUM];

#define HFDCAN(p) ((FDCAN_HandleTypeDef*)(p))

/* Private helpers -----------------------------------------------------------*/

/* byte_count → FDCAN DLC encoding */
static uint32_t dlc_bytes_to_code(drv_can_dlc_t dlc)
{
    if ((uint8_t)dlc <= 8) {
        return (uint32_t)dlc;
    }

    switch (dlc) {
    case DRV_CAN_DLC_12: return  9;
    case DRV_CAN_DLC_16: return 10;
    case DRV_CAN_DLC_20: return 11;
    case DRV_CAN_DLC_24: return 12;
    case DRV_CAN_DLC_32: return 13;
    case DRV_CAN_DLC_48: return 14;
    case DRV_CAN_DLC_64: return 15;
    default:             return  8;
    }
}

/* FDCAN DLC encoding → byte_count */
static drv_can_dlc_t dlc_code_to_bytes(uint32_t code)
{
    static const drv_can_dlc_t map[] = {
        DRV_CAN_DLC_0,  DRV_CAN_DLC_1,  DRV_CAN_DLC_2,  DRV_CAN_DLC_3,
        DRV_CAN_DLC_4,  DRV_CAN_DLC_5,  DRV_CAN_DLC_6,  DRV_CAN_DLC_7,
        DRV_CAN_DLC_8,  DRV_CAN_DLC_12, DRV_CAN_DLC_16, DRV_CAN_DLC_20,
        DRV_CAN_DLC_24, DRV_CAN_DLC_32, DRV_CAN_DLC_48, DRV_CAN_DLC_64,
    };

    if (code > 15) {
        return DRV_CAN_DLC_8;
    }
    return map[code];
}

/* Exported functions --------------------------------------------------------*/

/* --- 初始化 / 生命周期 --- */

drv_can_error_t drv_can_init(drv_can_channel_t ch, void* hcan)
{
    if (ch >= DRV_CAN_CH_NUM || !hcan) {
        return DRV_CAN_ERROR_INVALID_PARAM;
    }

    drv_can_ctx_t* ctx = &s_ctx[ch];

    if (ctx->initialized) {
        drv_can_deinit(ch);
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->hfdcan = HFDCAN(hcan);

    if (HAL_FDCAN_ConfigGlobalFilter(ctx->hfdcan,
            FDCAN_ACCEPT_IN_RX_FIFO0,
            FDCAN_ACCEPT_IN_RX_FIFO0,
            FDCAN_FILTER_REMOTE,
            FDCAN_REJECT_REMOTE) != HAL_OK) {
        return DRV_CAN_ERROR_UNINITIALIZED;
    }

    if (HAL_FDCAN_Start(ctx->hfdcan) != HAL_OK) {
        return DRV_CAN_ERROR_UNINITIALIZED;
    }

    if (HAL_FDCAN_ActivateNotification(ctx->hfdcan,
            FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
        HAL_FDCAN_Stop(ctx->hfdcan);
        return DRV_CAN_ERROR_UNINITIALIZED;
    }

    ctx->initialized = true;
    return DRV_CAN_OK;
}

void drv_can_deinit(drv_can_channel_t ch)
{
    if (ch >= DRV_CAN_CH_NUM) {
        return;
    }

    drv_can_ctx_t* ctx = &s_ctx[ch];
    if (!ctx->initialized) {
        return;
    }

    HAL_FDCAN_DeactivateNotification(ctx->hfdcan,
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE);
    HAL_FDCAN_Stop(ctx->hfdcan);
    memset(ctx, 0, sizeof(*ctx));
}

bool drv_can_is_initialized(drv_can_channel_t ch)
{
    if (ch >= DRV_CAN_CH_NUM) {
        return false;
    }
    return s_ctx[ch].initialized;
}

/* --- 发送 --- */

drv_can_error_t drv_can_send(drv_can_channel_t ch, const drv_can_msg_t* msg)
{
    if (ch >= DRV_CAN_CH_NUM || !msg) {
        return DRV_CAN_ERROR_INVALID_PARAM;
    }
    if (!s_ctx[ch].initialized) {
        return DRV_CAN_ERROR_UNINITIALIZED;
    }
    if ((uint8_t)msg->dlc > DRV_CAN_DLC_64) {
        return DRV_CAN_ERROR_INVALID_PARAM;
    }

    FDCAN_TxHeaderTypeDef tx = {
        .Identifier          = msg->id,
        .IdType              = msg->is_extended ? FDCAN_EXTENDED_ID
                                                : FDCAN_STANDARD_ID,
        .TxFrameType         = FDCAN_DATA_FRAME,
        .DataLength          = dlc_bytes_to_code(msg->dlc),
        .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
        .BitRateSwitch       = FDCAN_BRS_OFF,
        .FDFormat            = msg->dlc > 8 ? FDCAN_FD_CAN
                                            : FDCAN_CLASSIC_CAN,
        .TxEventFifoControl  = FDCAN_NO_TX_EVENTS,
        .MessageMarker       = 0,
    };

    if (HAL_FDCAN_AddMessageToTxFifoQ(s_ctx[ch].hfdcan, &tx,
            (uint8_t*)msg->data) != HAL_OK) {
        return DRV_CAN_ERROR_TX_BUSY;
    }

    return DRV_CAN_OK;
}

bool drv_can_tx_ready(drv_can_channel_t ch)
{
    if (ch >= DRV_CAN_CH_NUM || !s_ctx[ch].initialized) {
        return false;
    }
    return HAL_FDCAN_GetTxFifoFreeLevel(s_ctx[ch].hfdcan) > 0;
}

/* ===== HAL 回调 ===== */

/**
 * @brief FDCAN Rx FIFO 0 消息待处理回调
 *
 * 由 HAL_FDCAN_IRQHandler 内部触发。
 * 读取报文后调用用户回调。
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef* hfdcan,
    uint32_t RxFifo0ITs)
{
    (void)RxFifo0ITs;

    /* 查找通道 */
    drv_can_channel_t ch;
    for (ch = 0; ch < DRV_CAN_CH_NUM; ch++) {
        if (s_ctx[ch].initialized && s_ctx[ch].hfdcan == hfdcan) {
            break;
        }
    }
    if (ch >= DRV_CAN_CH_NUM) {
        return;
    }

    FDCAN_RxHeaderTypeDef rx;
    drv_can_msg_t msg;

    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rx,
            msg.data) != HAL_OK) {
        return;
    }

    msg.id = rx.Identifier;
    msg.is_extended = (rx.IdType == FDCAN_EXTENDED_ID);
    msg.dlc = dlc_code_to_bytes(rx.DataLength);

    if (s_ctx[ch].rx_callback) {
        s_ctx[ch].rx_callback(ch, &msg);
    }
}

/* --- 接收回调注册 --- */

drv_can_error_t drv_can_register_rx_callback(drv_can_channel_t ch,
    drv_can_rx_callback_t callback)
{
    if (ch >= DRV_CAN_CH_NUM) {
        return DRV_CAN_ERROR_INVALID_PARAM;
    }
    if (!s_ctx[ch].initialized) {
        return DRV_CAN_ERROR_UNINITIALIZED;
    }

    s_ctx[ch].rx_callback = callback;
    return DRV_CAN_OK;
}
