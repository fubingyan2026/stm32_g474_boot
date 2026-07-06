//
// Created by fubingyan on 2026-06-21.
//

/**
 * @file    sw_timer.c
 * @brief   FreeRTOS 风格软件定时器：单例、静态分配、统一时间轴、O(1) 中断检查
 *
 * 架构：
 * - 唯一有序 active_timer_list（按 target_time 升序），tick 只检查头部 O(1)
 * - HIGH 超时在 ISR 中回调 + 重插入
 * - NORMAL/LOW 超时移入事件队列，回调 + 重插入推迟到 task 上下文
 * - 绝对时间累加消除 Phase Drift
 * - 回调后 active 检查防止僵尸定时器
 */

#include "sw_timer.h"

#include "main.h"

#include <string.h>

/* Private constants ---------------------------------------------------------*/

#define ENTER_CRITICAL() __disable_irq()
#define EXIT_CRITICAL() __enable_irq()

/* Private variables ---------------------------------------------------------*/

static CLIST_HEAD(g_active_list); /**< 唯一有序延时链表（按 target_time 升序） */
static CLIST_HEAD(g_normal_queue); /**< NORMAL 事件队列 */
static CLIST_HEAD(g_low_queue); /**< LOW 事件队列 */
static volatile uint32_t g_tick; /**< 当前绝对滴答 */

/* Private function prototypes -----------------------------------------------*/

static void active_list_insert(sw_timer_t* t);
static void timer_rearm(sw_timer_t* t);

/* Exported functions --------------------------------------------------------*/

void sw_timer_init(sw_timer_t* t, const sw_timer_config_t* config)
{
    if (!t || !config) {
        return;
    }
    memset(t, 0, sizeof(sw_timer_t));
    t->config = *config;
    clist_init(&t->node);
}

void sw_timer_start(sw_timer_t* t, uint32_t delay_ticks, int32_t repeat_count)
{
    if (!t || repeat_count < 0) {
        return;
    }

    // 防御：delay_ticks == 0 且无限重复会导致 tick 死循环，强制最小值 1
    if (delay_ticks == 0) {
        delay_ticks = 1;
    }

    sw_timer_stop(t);

    t->target_time = g_tick + delay_ticks;
    t->period_ticks = delay_ticks;
    t->repeat_count = repeat_count;
    t->active = true;

    ENTER_CRITICAL();
    active_list_insert(t);
    EXIT_CRITICAL();
}

void sw_timer_stop(sw_timer_t* t)
{
    if (!t || !t->active) {
        return;
    }

    ENTER_CRITICAL();
    clist_del_init(&t->node);
    t->active = false;
    EXIT_CRITICAL();
}

void sw_timer_tick(uint32_t absolute_tick)
{
    g_tick = absolute_tick;

    // O(1)：只检查唯一链表头部
    while (!clist_empty(&g_active_list)) {
        sw_timer_t* t = clist_first_entry(&g_active_list, sw_timer_t, node);

        if ((int32_t)(t->target_time - g_tick) > 0) {
            break; // 头部未超时 → 后面都不会超时
        }

        clist_del_init(&t->node);

        switch (t->config.priority) {
        case SW_TIMER_PRIO_HIGH: {
            // HIGH：ISR 中直接回调
            sw_timer_cb_t cb = t->config.callback;
            void* ud = t->config.user_data;
            if (cb) {
                cb(ud);
            }

            // 修复：回调后检查 active，防止 stop() 后"僵尸复活"
            if (t->active && (t->repeat_count == 0 || t->repeat_count > 1)) {
                timer_rearm(t);
                active_list_insert(t);
            } else {
                t->active = false;
            }
            break;
        }
        case SW_TIMER_PRIO_NORMAL:
            // 推迟：只移入队列，不在 ISR 中回调/排序
            clist_add_tail(&g_normal_queue, &t->node);
            break;

        case SW_TIMER_PRIO_LOW:
            clist_add_tail(&g_low_queue, &t->node);
            break;
        }
    }
}

void sw_timer_task(void)
{
    sw_timer_t* t = NULL;

    ENTER_CRITICAL();
    if (!clist_empty(&g_normal_queue)) {
        t = clist_first_entry(&g_normal_queue, sw_timer_t, node);
        clist_del_init(&t->node);
    } else if (!clist_empty(&g_low_queue)) {
        t = clist_first_entry(&g_low_queue, sw_timer_t, node);
        clist_del_init(&t->node);
    }
    EXIT_CRITICAL();

    if (!t) {
        return;
    }

    // 任务上下文回调
    if (t->config.callback) {
        t->config.callback(t->config.user_data);
    }

    // 修复：回调后检查 active，防止 stop() 后"僵尸复活"
    if (t->active && (t->repeat_count == 0 || t->repeat_count > 1)) {
        timer_rearm(t);
        ENTER_CRITICAL();
        active_list_insert(t);
        EXIT_CRITICAL();
    } else {
        t->active = false;
    }
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief 有序插入 active_timer_list（调用者负责临界区保护）
 */
static void active_list_insert(sw_timer_t* t)
{
    if (clist_empty(&g_active_list)) {
        clist_add_tail(&g_active_list, &t->node);
        return;
    }

    sw_timer_t* pos;
    clist_for_each_entry(pos, &g_active_list, node)
    {
        if (pos->target_time > t->target_time) {
            clist_add_before(&pos->node, &t->node);
            return;
        }
    }
    clist_add_tail(&g_active_list, &t->node);
}

/**
 * @brief 递减 repeat_count 并重算 target_time（绝对时间累加，消除 Phase Drift）
 */
static void timer_rearm(sw_timer_t* t)
{
    if (t->repeat_count > 1) {
        t->repeat_count--;
    }
    // repeat_count == 0 为无限，不变

    // 修复：绝对时间累加，消除主循环延迟带来的周期漂移
    t->target_time += t->period_ticks;

    // 如果因长时间延迟错过多个周期，跳过已过期时间点
    if ((int32_t)(t->target_time - g_tick) <= 0) {
        t->target_time = g_tick + t->period_ticks;
    }
}
