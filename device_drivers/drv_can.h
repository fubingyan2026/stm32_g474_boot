/**
 * @file    drv_can.h
 * @author  maximillian
 * @version V1.2.0
 * @date    2026-07-6
 * @brief   CAN 设备驱动（FDCAN 经典/CAN FD，中断接收，多通道）
 * @attention
 *
 * STM32G474RBTx 使用 FDCAN 外设 (hfdcan1/hfdcan2)。
 * 支持经典 CAN (DLC 0-8) 与 CAN FD (DLC 0-64)。
 * 标准帧 11-bit ID，扩展帧 29-bit ID。
 */

#ifndef __DRV_CAN_H
#define __DRV_CAN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief CAN 通道枚举
 */
typedef enum {
    DRV_CAN_CH_1 = 0, /**< CAN1 — PA11(RX) / PA12(TX) */
    DRV_CAN_CH_2,     /**< CAN2 — PB12(RX) / PB13(TX) */
    DRV_CAN_CH_NUM,   /**< 通道总数 */
} drv_can_channel_t;

/**
 * @brief 驱动错误码
 */
typedef enum {
    DRV_CAN_OK = 0,
    DRV_CAN_ERROR_NULL_PTR,
    DRV_CAN_ERROR_UNINITIALIZED,
    DRV_CAN_ERROR_TX_BUSY,
    DRV_CAN_ERROR_INVALID_PARAM,
    DRV_CAN_ERROR_BUS_OFF,
} drv_can_error_t;

/**
 * @brief CAN FD 有效数据长度
 *
 * CAN FD 协议定义的离散数据长度：0-8, 12, 16, 20, 24, 32, 48, 64 字节。
 * 非标准值由驱动内部向上取整到最接近的有效值。
 */
typedef enum {
    DRV_CAN_DLC_0  = 0,
    DRV_CAN_DLC_1  = 1,
    DRV_CAN_DLC_2  = 2,
    DRV_CAN_DLC_3  = 3,
    DRV_CAN_DLC_4  = 4,
    DRV_CAN_DLC_5  = 5,
    DRV_CAN_DLC_6  = 6,
    DRV_CAN_DLC_7  = 7,
    DRV_CAN_DLC_8  = 8,
    DRV_CAN_DLC_12 = 12,
    DRV_CAN_DLC_16 = 16,
    DRV_CAN_DLC_20 = 20,
    DRV_CAN_DLC_24 = 24,
    DRV_CAN_DLC_32 = 32,
    DRV_CAN_DLC_48 = 48,
    DRV_CAN_DLC_64 = 64,
} drv_can_dlc_t;

/**
 * @brief CAN 报文
 */
typedef struct {
    uint32_t      id;          /**< CAN ID（标准 11-bit 或扩展 29-bit） */
    bool          is_extended; /**< true=扩展帧 */
    drv_can_dlc_t dlc;         /**< 数据长度（CAN FD 0-64 字节） */
    uint8_t       data[64];    /**< 数据负载（CAN FD 最大 64 字节） */
} drv_can_msg_t;

/** @brief CAN 接收回调函数类型（中断上下文执行） */
typedef void (*drv_can_rx_callback_t)(drv_can_channel_t ch, const drv_can_msg_t* msg);

/* Exported functions prototypes ---------------------------------------------*/

/* --- 初始化 / 生命周期 --- */

/**
 * @brief 初始化 CAN 通道
 * @param ch   通道号
 * @param hcan HAL 句柄 (FDCAN_HandleTypeDef*)，CubeMX 生成的 &hfdcan1/2
 */
drv_can_error_t drv_can_init(drv_can_channel_t ch, void* hcan);
void drv_can_deinit(drv_can_channel_t ch);
bool drv_can_is_initialized(drv_can_channel_t ch);

/* --- 发送 --- */

/**
 * @brief 发送 CAN 报文（非阻塞）
 * @return DRV_CAN_ERROR_TX_BUSY 表示无可用邮箱
 */
drv_can_error_t drv_can_send(drv_can_channel_t ch, const drv_can_msg_t* msg);

/**
 * @brief 查询 TX 邮箱是否空闲
 * @param ch 通道号
 * @return true=有空闲邮箱
 */
bool drv_can_tx_ready(drv_can_channel_t ch);

/* --- 接收回调 --- */

/**
 * @brief 注册接收回调（每通道独立注册）
 * @param ch       通道号
 * @param callback 回调函数（NULL=取消）
 * @note  回调在中断上下文中执行，应尽量简短
 */
drv_can_error_t drv_can_register_rx_callback(drv_can_channel_t ch,
    drv_can_rx_callback_t callback);

/* --- Bus-Off 检测 / 自恢复 --- */

/**
 * @brief 查询通道是否处于 Bus-Off 状态
 * @param ch 通道号
 * @return true=处于 Bus-Off（内核已置 CCCR.INIT 离线）
 */
bool drv_can_is_bus_off(drv_can_channel_t ch);

/**
 * @brief 从 Bus-Off 自动恢复（保留滤波器与接收回调）
 * @param ch 通道号
 * @return DRV_CAN_OK 表示已恢复或本就无需恢复
 * @note  内部经 HAL_FDCAN_Stop/Start 清 CCCR.INIT，触发内核 128×11 隐性位恢复，
 *        无需芯片复位。建议在周期任务中检测到 bus-off 时调用。
 */
drv_can_error_t drv_can_recover(drv_can_channel_t ch);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_CAN_H */
