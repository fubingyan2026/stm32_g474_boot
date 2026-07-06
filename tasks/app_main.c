/*
 * Copyright (c) 2022 HPMicro
 * Copyright (c) 2026 G1_Hand 项目组
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    app_main.c
 * @brief   G1_Hand 灵巧手主入口
 *
 * 初始化硬件后进入主循环，运行 RS-485 DMA 收发任务。
 */

#include "boot_task.h"
#include "drv_systick.h"
#include "led_task.h"
#include "log_task.h"
#include "sw_timer.h"

int app_main(void)
{
    /* 系统节拍（延时/时间戳） */
    delay_init();
    /* 日志输出（UART DMA） */
    log_task_init();
    /* LED 状态指示 */
    // led_task_init();

    /* 启动决策：有有效 App 则跳转，否则进入 bootloader */
    if (!boot_task_try_boot_app())
    {
        boot_task_init();
    }

    /* 主循环：所有周期性任务均由 sw_timer 驱动 */
    while (1) {
        sw_timer_tick(millis());
        sw_timer_task();
    }

    return 0;
}
