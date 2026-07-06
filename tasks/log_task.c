/*
 * Copyright (c) 2026 E1_PRO 项目组
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    log_task.c
 * @brief   日志输出任务实现（sw_timer 驱动，无需外部 poll）
 *
 * TX：sw_timer 定期检查 log 模块 kfifo，有数据则通过 UART DMA 发送。
 * RX：由 IDLE 中断驱动 drv_log_uart_sync_rx_dma 自动将数据推入 kfifo。
 */

#include "log_task.h"

#include "drv_systick.h"
#include "drv_log_uart.h"
#include "log.h"
#include "sw_timer.h"

/* Private constants ---------------------------------------------------------*/

#define LOG_TASK_TX_BUF_SIZE (256)
#define LOG_TASK_PERIOD_MS   (5U)

/* Private variables ---------------------------------------------------------*/

static uint8_t s_tx_buf[LOG_TASK_TX_BUF_SIZE];
static drv_log_uart_context_t s_log_uart_ctx;
static sw_timer_t s_log_timer;

/* Private function prototypes -----------------------------------------------*/

static void log_timer_cb(void* user_data);

/* Exported functions --------------------------------------------------------*/

void log_task_init(void)
{
    log_config_t log_cfg = {
        .name = "E1_PRO",
        .get_timestamp_cb = millis,
    };
    log_init(&log_cfg);

    drv_log_uart_init(&s_log_uart_ctx);

    /* 启动 sw_timer 驱动 TX 发送 */
    const sw_timer_config_t timer_cfg = {
        .priority = SW_TIMER_PRIO_NORMAL,
        .callback = log_timer_cb,
        .user_data = NULL,
    };
    sw_timer_init(&s_log_timer, &timer_cfg);
    sw_timer_start(&s_log_timer, LOG_TASK_PERIOD_MS, 0);
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief sw_timer 回调：发送待输出日志 + 读取接收数据
 */
static void log_timer_cb(void* user_data)
{
    (void)user_data;

    /* ── TX ── */
    if (!drv_log_uart_is_tx_busy(&s_log_uart_ctx)) {
        uint32_t log_len = log_tx_len();
        if (log_len > 0) {
            if (log_len > sizeof(s_tx_buf)) {
                log_len = sizeof(s_tx_buf);
            }
            uint32_t actual = log_tx_get(s_tx_buf, log_len);
            if (actual > 0) {
                drv_log_uart_send(&s_log_uart_ctx, s_tx_buf, actual);
            }
        }
    }

    /* ── RX ── */
    {
        uint8_t rx_buf[128];
        uint32_t rx_len = drv_log_uart_rx_read(rx_buf, sizeof(rx_buf));
        if (rx_len > 0) {
            log_hexdump("UART0", rx_buf, rx_len);
        }
    }
}
