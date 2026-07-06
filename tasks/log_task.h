/*
 * Copyright (c) 2026 E1_PRO 项目组
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    log_task.h
 * @brief   日志输出任务（sw_timer 驱动，无需外部 poll）
 */

#ifndef LOG_TASK_H
#define LOG_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

void log_task_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_TASK_H */
