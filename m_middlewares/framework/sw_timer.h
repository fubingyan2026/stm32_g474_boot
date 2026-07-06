//
// Created by fubingyan on 2026-06-21.
//

#ifndef __SW_TIMER_H
#define __SW_TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "clist.h"

#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 软件定时器优先级
 */
typedef enum {
    SW_TIMER_PRIO_HIGH = 0, /**< ISR 中直接回调 */
    SW_TIMER_PRIO_NORMAL, /**< 主循环 task 优先派发 */
    SW_TIMER_PRIO_LOW, /**< 主循环 task 次优先派发 */
} sw_timer_priority_t;

/**
 * @brief 定时器回调
 * @param user_data 用户数据
 */
typedef void (*sw_timer_cb_t)(void* user_data);

/**
 * @brief 软件定时器回调函数类型（结尾 _cb_t）
 */
typedef sw_timer_cb_t sw_timer_callback_t;

/**
 * @brief 软件定时器配置结构体
 */
typedef struct {
    sw_timer_priority_t priority; /**< 优先级 */
    sw_timer_callback_t callback; /**< 超时回调 */
    void* user_data; /**< 用户自定义数据 */
} sw_timer_config_t;

/**
 * @brief 软件定时器结构体（用户静态分配，嵌套配置）
 */
typedef struct {
    clist_head_t node; /**< 链表节点 */
    sw_timer_config_t config; /**< 嵌套配置（init 时确定） */

    // 运行时状态
    uint32_t target_time; /**< 绝对超时滴答 */
    uint32_t period_ticks; /**< 重复间隔 */
    int32_t repeat_count; /**< 0=无限, 1=单次, >1=剩余次数 */
    bool active; /**< 是否已启动 */
} sw_timer_t;

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 静态初始化定时器
 * @param t 定时器指针（用户静态分配）
 * @param config 配置结构体指针
 */
void sw_timer_init(sw_timer_t* t, const sw_timer_config_t* config);

/**
 * @brief 启动/重启定时器
 * @param t 定时器指针
 * @param delay_ticks 延时滴答（0=立即）
 * @param repeat_count 重复次数（0=无限, 1=单次, N=精确N次）
 */
void sw_timer_start(sw_timer_t* t, uint32_t delay_ticks, int32_t repeat_count);

/**
 * @brief 停止定时器（从所有链表中安全移除）
 * @param t 定时器指针
 */
void sw_timer_stop(sw_timer_t* t);

/**
 * @brief 滴答处理（SysTick ISR 中调用）
 * @param absolute_tick 当前绝对滴答计数值
 */
void sw_timer_tick(uint32_t absolute_tick);

/**
 * @brief 任务处理（主循环中调用）
 */
void sw_timer_task(void);

#ifdef __cplusplus
}
#endif

#endif /* __SW_TIMER_H */
