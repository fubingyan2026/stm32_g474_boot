//
// Created by fubingyan on 25-4-12.
//

/**
 * @file    key_base.c
 * @author  fubingyan
 * @version V1.0.0
 * @date    2025-04-12
 * @brief   按键状态机模块实现
 * @attention
 *
 * Copyright (c) 2025 Company Name.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "key_base.h"
#include "string.h"
#include "log.h"
/* Private constants ---------------------------------------------------------*/

#define KEY_BASE_DEBOUNCE_TIME_MS 50
#define KEY_BASE_MIN_TIME_THRESHOLD_MS 500
#define KEY_BASE_DEBOUNCE_COUNT 3

/* Private variables ---------------------------------------------------------*/

static CLIST_HEAD(s_key_list);
static uint16_t s_key_count = 0;
static bool s_system_initialized = false;

/* Private function prototypes -----------------------------------------------*/

static uint32_t key_base_time_slice(uint32_t new_time, uint32_t old_time);
static void key_base_filter(key_base_context_t* ctx,
    uint32_t current_time);
static void key_base_fsm_step(key_base_context_t* ctx,
    uint32_t current_time);

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化按键系统
 */
void key_base_init(void)
{
    if (s_system_initialized) {
        return;
    }
    clist_init(&s_key_list);
    s_key_count = 0;
    s_system_initialized = true;
}

/**
 * @brief 反初始化按键系统，释放所有资源
 */
void key_base_deinit(void)
{
    if (!s_system_initialized) {
        return;
    }

    key_base_context_t *ctx, *tmp;
    clist_for_each_entry_safe(ctx, tmp, &s_key_list, list_node)
    {
        clist_del(&ctx->list_node);
        ctx->initialized = false;

        if (!ctx->is_static) {
            // __free(ctx);
        }
    }

    clist_init(&s_key_list);
    s_key_count = 0;
    s_system_initialized = false;
}

/**
 * @brief 获取按键实例数量
 * @return 按键实例数量
 */
uint16_t key_base_get_count(void) { return s_key_count; }

/**
 * @brief 注册按键实例（静态内存版本）
 * @param config 按键配置指针
 * @param instance 静态内存指针
 * @return 错误码
 */
key_base_error_t key_base_register_static(const key_base_config_t* config,
    key_base_context_t* instance)
{
    if (!config || !config->name || !instance) {
        return KEY_BASE_ERROR_INVALID_PARAM;
    }

    if (!config->read_pin_cb || !config->event_callback
        || !config->get_time_cb) {
        LOG_I("key_base",
            "key_base_register_static failed: missing callback");
        return KEY_BASE_ERROR_INVALID_PARAM;
    }

    if (!s_system_initialized) {
        key_base_init();
    }

    key_base_context_t* existing = key_base_get_instance(config->name);
    if (existing) {
        LOG_I("key_base",
            "key_base_register_static: key %s already exists", config->name);
        return KEY_BASE_ERROR_ALREADY_EXIST;
    }

    memset(instance, 0, sizeof(key_base_context_t));
    instance->config = *config;
    instance->is_static = 1;
    instance->initialized = true;

    clist_add_tail(&s_key_list, &instance->list_node);
    s_key_count++;

    LOG_I("key_base", "key_base_register_static success: %s (total: %u)",
        instance->config.name, s_key_count);

    return KEY_BASE_OK;
}

/**
 * @brief 删除按键实例
 * @param name 按键名称
 * @return 错误码
 */
key_base_error_t key_base_unregister(const char* name)
{
    if (!name) {
        LOG_I("key_base", "key_base_unregister failed: name is NULL");
        return KEY_BASE_ERROR_INVALID_PARAM;
    }

    if (!s_system_initialized) {
        LOG_I("key_base",
            "key_base_unregister failed: system not initialized");
        return KEY_BASE_ERROR_INVALID_PARAM;
    }

    key_base_context_t* ctx;
    clist_for_each_entry(ctx, &s_key_list, list_node)
    {
        if (strcmp(ctx->config.name, name) == 0) {
            clist_del(&ctx->list_node);
            ctx->initialized = false;

            if (!ctx->is_static) {
                // __free(ctx);
            } else {
                LOG_I("key_base",
                    "key_base_unregister: static key %s skip free", name);
            }

            s_key_count--;
            LOG_I("key_base",
                "key_base_unregister success: %s (remaining: %u)",
                name, s_key_count);
            return KEY_BASE_OK;
        }
    }

    LOG_I("key_base", "key_base_unregister failed: key %s not found",
        name);
    return KEY_BASE_ERROR_NOT_FOUND;
}

/**
 * @brief 按键任务处理函数
 */
void key_base_task(void)
{
    if (!s_system_initialized || clist_empty(&s_key_list)) {
        return;
    }

    uint32_t current_time = 0;
    bool have_time = false;
    key_base_context_t* ctx;

    clist_for_each_entry(ctx, &s_key_list, list_node)
    {
        if (!have_time) {
            if (ctx->config.get_time_cb == NULL)
                continue;
            current_time = ctx->config.get_time_cb();
            have_time = true;
        }

        if (ctx->config.read_pin_cb != NULL) {
            key_base_filter(ctx, current_time);
        }
        if (ctx->config.event_callback != NULL) {
            key_base_fsm_step(ctx, current_time);
        }
    }
}

/**
 * @brief 获取按键实例
 * @param name 按键名称
 * @return 按键实例指针，未找到返回NULL
 */
key_base_context_t* key_base_get_instance(const char* name)
{
    if (!name || !s_system_initialized) {
        return NULL;
    }
    key_base_context_t* ctx;
    clist_for_each_entry(ctx, &s_key_list, list_node)
    {
        if (strcmp(ctx->config.name, name) == 0) {
            return ctx;
        }
    }
    return NULL;
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief 计算时间差（处理溢出）
 * @param new_time 新时间
 * @param old_time 旧时间
 * @return 时间差
 */
static uint32_t key_base_time_slice(uint32_t new_time, uint32_t old_time)
{
    return new_time - old_time;
}

/**
 * @brief 消抖处理函数
 * @param ctx 按键上下文指针
 * @param current_time 当前时间（由调用方缓存）
 */
static void key_base_filter(key_base_context_t* ctx,
    uint32_t current_time)
{
    ctx->timer = current_time;
    ctx->diff_timer = key_base_time_slice(current_time,
        ctx->last_timer);
    ctx->last_pin_state = ctx->pin_state;
    ctx->last_timer = current_time;

    if (ctx->diff_timer == 0)
        return;

    uint8_t current_pin = ctx->config.read_pin_cb();

    if (current_pin == KEY_BASE_PIN_STATE_PRESS) {
        if (ctx->pin_state != KEY_BASE_PIN_STATE_PRESS) {
            ctx->press_debounce_count++;
            if (ctx->diff_timer >= KEY_BASE_DEBOUNCE_TIME_MS
                || ctx->press_debounce_count >= KEY_BASE_DEBOUNCE_COUNT) {
                ctx->press_debounce_count = 0;
                ctx->pin_state = KEY_BASE_PIN_STATE_PRESS;
                ctx->press_start_time = current_time;
            }
        }
        ctx->release_debounce_count = 0;
    } else {
        if (ctx->pin_state == KEY_BASE_PIN_STATE_PRESS) {
            ctx->release_debounce_count++;
            if (ctx->diff_timer >= KEY_BASE_DEBOUNCE_TIME_MS
                || ctx->release_debounce_count >= KEY_BASE_DEBOUNCE_COUNT) {
                ctx->release_debounce_count = 0;
                ctx->pin_state = KEY_BASE_PIN_STATE_RELEASE;
            }
        }
        ctx->press_debounce_count = 0;
    }
}

/**
 * @brief 状态机处理函数
 * @param ctx 按键上下文指针
 * @param current_time 当前时间（由调用方缓存）
 */
static void key_base_fsm_step(key_base_context_t* ctx,
    uint32_t current_time)
{
    ctx->timer = current_time;

    const uint32_t effective_long_press_time = (ctx->config.long_press_time_ms < KEY_BASE_MIN_TIME_THRESHOLD_MS)
        ? KEY_BASE_MIN_TIME_THRESHOLD_MS
        : ctx->config.long_press_time_ms;
    const uint32_t cooling_window = effective_long_press_time / 2;

    if (ctx->post_long_release_time
        && key_base_time_slice(ctx->timer,
               ctx->post_long_release_time)
            < cooling_window) {
        if (ctx->pin_state == KEY_BASE_PIN_STATE_PRESS) {
            return;
        }
    }

    const uint32_t click_window = (ctx->config.multi_click_time_ms > 0)
        ? ctx->config.multi_click_time_ms
        : effective_long_press_time;

    if (ctx->last_pin_state != ctx->pin_state) {
        if (ctx->pin_state == KEY_BASE_PIN_STATE_PRESS) {
            if (key_base_time_slice(ctx->timer, ctx->release_time)
                >= effective_long_press_time) {
                ctx->key_event = KEY_BASE_EVENT_LONG_WAIT_PRESS;
                ctx->config.event_callback(ctx->key_event, ctx);
            } else {
                ctx->key_event = KEY_BASE_EVENT_CLICK;
                ctx->config.event_callback(ctx->key_event, ctx);
            }
        } else {
            ctx->release_time = ctx->timer;
            if (ctx->long_hold_state) {
                ctx->long_hold_state = false;
                ctx->key_event = KEY_BASE_EVENT_LONG_HOLD_RELEASE;
                ctx->batter_event = KEY_BASE_BATTER_STATE_IDLE;
                ctx->config.event_callback(ctx->key_event, ctx);
            } else {
                ctx->key_event = KEY_BASE_EVENT_DOWN;
                ctx->config.event_callback(ctx->key_event, ctx);
            }
        }
    } else {
        if (ctx->pin_state == KEY_BASE_PIN_STATE_PRESS) {
            if (!ctx->long_hold_state) {
                if (key_base_time_slice(ctx->timer,
                        ctx->press_start_time)
                    >= effective_long_press_time) {
                    ctx->long_hold_state = true;
                    ctx->key_event = KEY_BASE_EVENT_LONG_HOLD;
                    ctx->post_long_release_time = ctx->timer;
                    ctx->config.event_callback(ctx->key_event, ctx);
                }
            }
        }
    }

    switch (ctx->batter_event) {
    case KEY_BASE_BATTER_STATE_IDLE:
        if (ctx->last_key_event != ctx->key_event) {
            if (ctx->pin_state == KEY_BASE_PIN_STATE_PRESS) {
                ctx->batter_counts = 0;
                ctx->batter_event = KEY_BASE_BATTER_STATE_WAIT;
                ctx->batter_reset_time = ctx->timer;
            }
        }
        break;

    case KEY_BASE_BATTER_STATE_WAIT:
        if (key_base_time_slice(ctx->timer, ctx->batter_reset_time)
            <= click_window) {
            if (ctx->last_key_event != ctx->key_event) {
                ctx->batter_reset_time = ctx->timer;
                if (ctx->key_event == KEY_BASE_EVENT_DOWN) {
                    ctx->batter_counts++;
                }
            }
        } else {
            if (ctx->pin_state == KEY_BASE_PIN_STATE_RELEASE) {
                if (ctx->batter_counts > 0) {
                    static const key_base_event_t click_map[] = {
                        [0] = KEY_BASE_EVENT_ONE_CLICK,
                        [1] = KEY_BASE_EVENT_DOUBLE_CLICK,
                        [2] = KEY_BASE_EVENT_TRIPLE_CLICK,
                        [3] = KEY_BASE_EVENT_REPEAT_CLICK
                    };
                    const uint8_t cnt = ctx->batter_counts - 1U
                            > sizeof(click_map) - 1U
                        ? sizeof(click_map) - 1U
                        : ctx->batter_counts - 1U;
                    ctx->key_event = click_map[cnt];
                    ctx->config.event_callback(ctx->key_event, ctx);
                }
                ctx->batter_counts = 0;
                ctx->batter_event = KEY_BASE_BATTER_STATE_IDLE;
            }
        }
        break;

    default:
        break;
    }

    ctx->last_key_event = ctx->key_event;
}
