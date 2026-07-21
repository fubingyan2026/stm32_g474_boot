/**
 * @file    drv_log_uart.h
 * @author  G1_Hand 项目组
 * @version V1.2.0
 * @date    2025-06-12
 * @brief   USART1 设备驱动（DMA 输出 + DMA circular + IDLE 中断接收）
 * @attention
 *
 * Copyright (c) 2025 G1_Hand 项目组.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 *
 */

#ifndef __DRV_LOG_UART_H
#define __DRV_LOG_UART_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief USART1 驱动错误码枚举
 */
typedef enum {
    DRV_LOG_UART_OK = 0,                /**< 操作成功 */
    DRV_LOG_UART_ERROR_NULL_PTR,        /**< 空指针错误 */
    DRV_LOG_UART_ERROR_UNINITIALIZED,   /**< 未初始化 */
    DRV_LOG_UART_ERROR_TX_BUSY,         /**< TX DMA 忙 */
} drv_log_uart_error_t;

/* Exported functions prototypes ---------------------------------------------*/

/* --- 初始化 / 生命周期 --- */

/** @brief 初始化日志串口（内部状态，无需传参） */
drv_log_uart_error_t drv_log_uart_init(void);
void drv_log_uart_deinit(void);
bool drv_log_uart_is_initialized(void);

/* --- TX（日志输出） --- */

/**
 * @brief 非阻塞 DMA 发送
 * @param data 数据指针
 * @param len  数据长度
 * @return 操作结果错误码
 */
drv_log_uart_error_t drv_log_uart_send(const uint8_t* data, uint32_t len);

bool drv_log_uart_is_tx_busy(void);

/* --- RX（DMA circular + IDLE 中断 → kfifo） --- */

uint32_t drv_log_uart_rx_read(uint8_t* buf, uint32_t max_len);
uint32_t drv_log_uart_rx_available(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_LOG_UART_H */
