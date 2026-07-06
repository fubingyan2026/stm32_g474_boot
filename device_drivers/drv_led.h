/*
 * Copyright (c) 2026 G1_Hand 项目组
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    drv_led.h
 * @brief   LED 设备驱动
 *
 * 管理 PF02 状态指示 LED 的 GPIO 控制。
 * 引脚配置为推挽输出，低电平点亮。
 */

#ifndef DRV_LED_H
#define DRV_LED_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * 硬件配置宏
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化 LED 硬件
 *
 * 配置 PF02 引脚为 GPIO 推挽输出：
 * - 设置 IOC 引脚复用为 GPIO 功能
 * - 使能 GPIO 时钟
 * - 设置初始输出电平为 OFF
 *
 * 应在 board_init() 之后调用一次。
 */
void drv_led_init(void);

/**
 * @brief 翻转 LED 输出电平
 */
void drv_led_toggle(void);

/**
 * @brief 设置 LED 状态
 *
 * @param on  true = 点亮，false = 熄灭
 */
void drv_led_set(bool on);

bool drv_led_read(void);

#ifdef __cplusplus
}
#endif

#endif /* DRV_LED_H */
