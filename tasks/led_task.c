/*
 * Copyright (c) 2026 E1_PRO 项目组
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    led_task.c
 * @brief   LED 状态指示任务实现（sw_timer 驱动，无需外部 poll）
 */

#include "led_task.h"
#include "drv_systick.h"
#include "led.h"
#include "sw_timer.h"

#define LED_TASK_DEFAULT_BLINK_COUNT 3
#define LED_TASK_DEFAULT_BLINK_CYCLE_MS 200
#define LED_TASK_DEFAULT_BLINK_WAIT_MS 1000

/** @brief LED 刷新周期 (ms) */
#define LED_TASK_REFRESH_PERIOD_MS (10U)

static led_handle_t s_led_handle;
static sw_timer_t s_led_timer;

static void led_task_write_pin(uint16_t value)
{
    (void)value;
}

static void led_task_on_state_change(led_handle_t* instance,
    led_state_t new_state, void* user_data)
{
    (void)user_data;

    if (new_state == LED_STATE_OFF) {
        led_task_start_blink(LED_TASK_DEFAULT_BLINK_COUNT,
            LED_TASK_DEFAULT_BLINK_CYCLE_MS);
    }
}

/**
 * @brief sw_timer 回调：驱动 LED FSM
 */
static void led_timer_cb(void* user_data)
{
    (void)user_data;
    led_task_refresh();
}

void led_task_start_blink(uint16_t count, uint16_t cycle_ms)
{
    led_cmd_t cmd = {
        .led_set_state = LED_STATE_BLINK_CODE,
        .led_blink_cycle_ms = cycle_ms,
        .led_blink_wait_ms = LED_TASK_DEFAULT_BLINK_WAIT_MS,
        .led_blink_code_counts = count,
    };

    led_set_blink_interval(&s_led_handle, &cmd);
    led_set_state(&s_led_handle, LED_STATE_BLINK_CODE);
}

void led_task_init(void)
{
    led_init(millis);

    led_config_t config = {
        .name = "status",
        .init_state = LED_STATE_OFF,
        .write_pin = led_task_write_pin,
    };
    led_register_static(&config, &s_led_handle);
    led_set_callbacks(&s_led_handle, led_task_on_state_change, NULL, NULL, NULL);

    led_task_start_blink(LED_TASK_DEFAULT_BLINK_COUNT,
        LED_TASK_DEFAULT_BLINK_CYCLE_MS);

    /* 启动 sw_timer 驱动 LED 刷新 */
    const sw_timer_config_t timer_cfg = {
        .priority = SW_TIMER_PRIO_NORMAL,
        .callback = led_timer_cb,
        .user_data = NULL,
    };
    sw_timer_init(&s_led_timer, &timer_cfg);
    sw_timer_start(&s_led_timer, LED_TASK_REFRESH_PERIOD_MS, 0);
}
