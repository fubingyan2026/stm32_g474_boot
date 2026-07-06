/*
 * Copyright (c) 2026 E1_PRO 项目组
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    led_task.h
 * @brief   LED 状态指示任务（sw_timer 驱动）
 */

#ifndef LED_TASK_H
#define LED_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void led_task_init(void);

/**
 * @brief 启动 LED 编码闪烁
 * @param count    闪烁次数（0 = 无限循环）
 * @param cycle_ms 闪烁半周期 (ms)
 */
void led_task_start_blink(uint16_t count, uint16_t cycle_ms);

#ifdef __cplusplus
}
#endif

#endif /* LED_TASK_H */
