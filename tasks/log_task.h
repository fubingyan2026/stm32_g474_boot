/*
 * Copyright (c) 2026 E1_PRO 项目组
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    log_task.h
 * @brief   日志输出任务（sw_timer 驱动，无需外部 poll）
 *
 * 支持 UART / SEGGER RTT 两种输出后端，默认 UART。
 * 通过 log_task_set_output() 在运行时切换。
 */

#ifndef LOG_TASK_H
#define LOG_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 日志输出后端
 */
typedef enum {
    LOG_OUTPUT_NONE = 0, /**< 关闭输出 */
    LOG_OUTPUT_UART, /**< USART1 DMA 输出（默认） */
    LOG_OUTPUT_RTT, /**< SEGGER RTT 输出 */
} log_task_output_t;

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化日志任务
 * @note  初始化 log 模块 + 启动 sw_timer，输出后端默认为 UART
 */
void log_task_init(void);

/**
 * @brief 切换日志输出后端
 * @param mode  输出后端
 * @note  可在初始化后任意时刻调用，实时生效
 */
void log_task_set_output(log_task_output_t mode);

#ifdef __cplusplus
}
#endif

#endif /* LOG_TASK_H */
