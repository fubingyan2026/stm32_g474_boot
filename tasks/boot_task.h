/**
 * @file    boot_task.h
 * @brief   Bootloader 主任务 — 胶水层，串联 CAN ISR → 状态机 → Flash
 */

#ifndef __BOOT_TASK_H
#define __BOOT_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 上电启动决策：检查 Metadata，决定跳转 App 或进入 bootloader
 * @return true 表示已跳转到 App（不会返回），false 表示进入 bootloader 模式
 */
bool boot_task_try_boot_app(void);

/**
 * @brief 初始化 bootloader 主任务
 *
 * 初始化 CAN、Flash、状态机，注册 ISR 回调，创建轮询定时器。
 * 调用后进入 CAN 升级监听循环。
 */
void boot_task_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_TASK_H */
