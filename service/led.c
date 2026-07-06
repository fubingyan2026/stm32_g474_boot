//
// Created by fubingyan on 25-9-20.
//

/**
 * @file    led.c
 * @brief   LED 控制模块实现
 * @note    通过 FSM 管理 ON/OFF/BLINK_CODE/BREATHING 四种工作状态。
 *          命令通过 kfifo 异步队列传递，支持闪烁/呼吸参数热更新。
 *          引脚通过 uint16_t PWM 接口回调（0=灭, 1023=最亮）。
 */

/* Includes ------------------------------------------------------------------*/
#include "led.h"

#include <string.h>
#include "msg_fifo.h"
#include "maths.h"

/* Private constants ---------------------------------------------------------*/

#define LED_PWM_MAX         1023U
#define LED_GAMMA           2.2f

/* Private variables ---------------------------------------------------------*/

static clist_head_t s_led_head;
static led_get_time_cb_t s_led_get_time;
static bool s_led_initialized;
static msg_fifo_t s_cmd_fifo;

/* Private function prototypes -----------------------------------------------*/

static void led_phys_write(led_handle_t* handle, uint16_t value);
static fsm_state_t led_fsm_static_handler(fsm_t* ctx);
static fsm_state_t led_fsm_blink_handler(fsm_t* ctx);
static fsm_state_t led_fsm_breathing_handler(fsm_t* ctx);
static void led_fsm_on_entry(fsm_t* ctx, fsm_state_t state);
static void led_process_cmds(led_handle_t* handle);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief 物理写引脚 PWM
 */
static void led_phys_write(led_handle_t* handle, uint16_t value)
{
    handle->last_write_value = value;
    handle->config.write_pin(value);

    if (handle->edge_cb) {
        ((led_edge_cb_t)handle->edge_cb)(handle, value > 0,
            handle->callback_user_data);
    }
}

/* FSM handlers --------------------------------------------------------------*/

/** @brief 无需定时逻辑的稳定状态（NONE/OFF/ON）统一用此 handler */
static fsm_state_t led_fsm_static_handler(fsm_t* ctx)
{
    return fsm_current_state(ctx);
}

/**
 * @brief FSM BLINK_CODE 状态处理：编码闪烁
 */
static fsm_state_t led_fsm_blink_handler(fsm_t* ctx)
{
    led_handle_t* handle = (led_handle_t*)fsm_user_data(ctx);
    uint32_t now = s_led_get_time ? s_led_get_time() : 0;

    if (handle->blink_code_phase == LED_BLINK_PHASE_BLINKING) {
        if ((uint32_t)(now - handle->last_toggle_time) >= handle->current_cmd.led_blink_cycle_ms) {
            handle->last_toggle_time = now;
            handle->blink_sw_on = !handle->blink_sw_on;
            led_phys_write(handle, handle->blink_sw_on ? LED_PWM_MAX : 0);
            if (!handle->blink_sw_on) {
                handle->current_led_blink_code_counts++;
                if (handle->current_cmd.led_blink_code_counts == 0 ||
                    handle->current_led_blink_code_counts >= handle->current_cmd.led_blink_code_counts) {
                    handle->blink_code_phase_last = handle->blink_code_phase;
                    handle->blink_code_phase = LED_BLINK_PHASE_INTERVAL;
                    handle->interval_start_time = now;
                    handle->current_led_blink_code_counts = 0;
                    if (handle->current_cmd.led_blink_code_counts > 0) {
                        return LED_STATE_OFF;
                    }
                }
            }
        }
    } else {
        if ((uint32_t)(now - handle->interval_start_time) >= handle->current_cmd.led_blink_wait_ms) {
            handle->blink_code_phase = LED_BLINK_PHASE_BLINKING;
            handle->last_toggle_time = now;
        }
    }
    return LED_STATE_BLINK_CODE;
}

/**
 * @brief FSM BREATHING 状态处理：呼吸灯
 */
static fsm_state_t led_fsm_breathing_handler(fsm_t* ctx)
{
    led_handle_t* handle = (led_handle_t*)fsm_user_data(ctx);
    uint32_t now = s_led_get_time ? s_led_get_time() : 0;

    if ((uint32_t)(now - handle->last_breath_time) >= handle->breath_step_ms) {
        handle->last_breath_time = now;

        uint16_t total_steps = handle->breath_cycle_ms / handle->breath_step_ms;
        if (total_steps == 0) total_steps = 1;

        float phase = (float)handle->breath_cycle * 2.0f * M_PIf / (float)total_steps;
        float brightness = (sin_approx(phase) + 1.0f) * 0.5f;
        float gamma = powerf(brightness, LED_GAMMA);
        uint16_t range = handle->breath_max_duty - handle->breath_min_duty;
        handle->breath_value = handle->breath_min_duty + (uint16_t)(gamma * (float)range);
        led_phys_write(handle, handle->breath_value);

        handle->breath_cycle++;
        if (handle->breath_cycle >= total_steps) {
            handle->breath_cycle = 0;
        }
    }

    return LED_STATE_BREATHING;
}

/* FSM entry / exit ----------------------------------------------------------*/

static void led_fsm_on_entry(fsm_t* ctx, fsm_state_t state)
{
    led_handle_t* handle = (led_handle_t*)fsm_user_data(ctx);

    if (handle->state_change_cb) {
        ((led_state_change_cb_t)handle->state_change_cb)(handle, state,
            handle->callback_user_data);
    }

    switch (state) {
    case LED_STATE_ON:
        led_phys_write(handle, LED_PWM_MAX);
        break;
    case LED_STATE_OFF:
        led_phys_write(handle, 0);
        break;
    case LED_STATE_BLINK_CODE:
        handle->current_led_blink_code_counts = 0;
        handle->blink_code_phase_last = handle->blink_code_phase;
        handle->blink_code_phase = LED_BLINK_PHASE_INTERVAL;
        handle->interval_start_time = s_led_get_time ? s_led_get_time() : 0;
        handle->blink_sw_on = false;
        led_phys_write(handle, 0);
        break;
    case LED_STATE_BREATHING: {
        handle->last_breath_time = s_led_get_time ? s_led_get_time() : 0;
        handle->breath_value = handle->last_write_value;

        uint16_t total_steps = handle->breath_cycle_ms / handle->breath_step_ms;
        if (total_steps == 0) total_steps = 1;

        uint16_t mid = (uint16_t)(((uint32_t)handle->breath_min_duty + handle->breath_max_duty) / 2U);
        handle->breath_cycle = (handle->last_write_value >= mid)
            ? (uint16_t)((uint32_t)total_steps * 1U / 4U)
            : (uint16_t)((uint32_t)total_steps * 3U / 4U);
        break;
    }
    default:
        break;
    }
}

/* Command processing --------------------------------------------------------*/

static void led_process_cmds(led_handle_t* handle)
{
    led_cmd_t cmd;
    while (msg_fifo_pop(handle->cmd_fifo, &cmd)) {
        if (cmd.led_set_state != LED_STATE_NONE) {
            fsm_goto(&handle->fsm, cmd.led_set_state);
        }

        if (cmd.led_set_state == LED_STATE_BLINK_CODE) {
            if (handle->pending_blink_update && fsm_current_state(&handle->fsm) == LED_STATE_BLINK_CODE) {
                if (handle->blink_sw_on) {
                    msg_fifo_push(handle->cmd_fifo, &cmd);
                    break;
                }
                handle->pending_blink_update = false;
            }

            if (cmd.led_blink_cycle_ms > 0)
                handle->current_cmd.led_blink_cycle_ms = cmd.led_blink_cycle_ms;
            if (cmd.led_blink_wait_ms > 0)
                handle->current_cmd.led_blink_wait_ms = cmd.led_blink_wait_ms;
            if (cmd.led_blink_code_counts > 0)
                handle->current_cmd.led_blink_code_counts = cmd.led_blink_code_counts;

            if (fsm_current_state(&handle->fsm) == LED_STATE_BLINK_CODE) {
                handle->current_led_blink_code_counts = 0;
                handle->blink_code_phase = LED_BLINK_PHASE_INTERVAL;
                handle->interval_start_time = s_led_get_time ? s_led_get_time() : 0;
                handle->blink_sw_on = false;
                led_phys_write(handle, 0);
            }
        }

        if (cmd.led_set_state == LED_STATE_BREATHING || cmd.led_breath_cycle_ms > 0) {
            if (cmd.led_breath_cycle_ms > 0)
                handle->breath_cycle_ms = cmd.led_breath_cycle_ms;
            if (cmd.led_breath_min_duty < 0xFFFF)
                handle->breath_min_duty = cmd.led_breath_min_duty;
            if (cmd.led_breath_max_duty < 0xFFFF)
                handle->breath_max_duty = cmd.led_breath_max_duty;
            handle->breath_step_ms = handle->breath_cycle_ms / 66;
            if (handle->breath_step_ms < 10)
                handle->breath_step_ms = 10;
        }
    }
}

/* Exported functions --------------------------------------------------------*/

led_error_t led_init(led_get_time_cb_t get_time_cb)
{
    if (get_time_cb == NULL)
        return LED_ERROR_INVALID_PARAM;
    if (s_led_initialized)
        return LED_OK_EXISTED;

    s_led_get_time = get_time_cb;
    clist_init(&s_led_head);
    s_led_initialized = true;
    return LED_OK;
}

void led_deinit(void)
{
    if (!s_led_initialized)
        return;

    clist_head_t *pos, *tmp;
    clist_for_each_safe(pos, tmp, &s_led_head)
    {
        led_handle_t* h = clist_entry(pos, led_handle_t, node);
        clist_del(pos);
        if (h->cmd_fifo)
            kfifo_reset(&h->cmd_fifo->fifo);
    }

    clist_init(&s_led_head);
    s_led_initialized = false;
}

led_handle_t* led_get_instance(const char* name)
{
    if (name == NULL || !s_led_initialized)
        return NULL;

    led_handle_t* h;
    clist_for_each_entry(h, &s_led_head, node)
    {
        if (strcmp(h->config.name, name) == 0)
            return h;
    }
    return NULL;
}

clist_head_t* led_get_head(void)
{
    return s_led_initialized ? &s_led_head : NULL;
}

led_error_t led_register_static(const led_config_t* config,
    led_handle_t* instance)
{
    if (config == NULL || instance == NULL || config->name == NULL || config->write_pin == NULL) {
        return LED_ERROR_INVALID_PARAM;
    }
    if (!s_led_initialized)
        return LED_ERROR_INTERNAL;
    if (led_get_instance(config->name))
        return LED_ERROR_ALREADY_EXIST;

    memset(instance, 0, sizeof(led_handle_t));
    memcpy(&instance->config, config, sizeof(led_config_t));

    instance->breath_cycle_ms = config->breath_cycle_ms ? config->breath_cycle_ms : LED_BREATH_CYCLE_MS_DEFAULT;
    instance->breath_step_ms  = config->breath_step_ms  ? config->breath_step_ms  : LED_BREATH_STEP_MS_DEFAULT;
    instance->breath_min_duty = config->breath_min_duty ? config->breath_min_duty : LED_BREATH_MIN_DUTY_DEFAULT;
    instance->breath_max_duty = config->breath_max_duty ? config->breath_max_duty : LED_BREATH_MAX_DUTY_DEFAULT;

    /* 初始化 FSM */
    static const char* names[] = { "NONE", "OFF", "ON", "BLINK", "BREATH" };
    static fsm_handler_t handlers[LED_STATE_MAX];
    static fsm_guard_t transitions[LED_STATE_MAX * LED_STATE_MAX];
    memset(handlers, 0, sizeof(handlers));
    memset(transitions, 0, sizeof(transitions));

    handlers[LED_STATE_NONE]       = led_fsm_static_handler;
    handlers[LED_STATE_OFF]        = led_fsm_static_handler;
    handlers[LED_STATE_ON]         = led_fsm_static_handler;
    handlers[LED_STATE_BLINK_CODE] = led_fsm_blink_handler;
    handlers[LED_STATE_BREATHING]  = led_fsm_breathing_handler;

    fsm_config_t fsm_cfg = {
        .handlers = handlers,
        .transitions = transitions,
        .state_count = LED_STATE_MAX,
        .entry_cb = led_fsm_on_entry,
        .exit_cb = NULL,
        .state_names = names,
        .user_data = instance,
    };
    fsm_fill(&fsm_cfg, fsm_always_true);
    fsm_init(&instance->fsm, config->init_state, &fsm_cfg);

    static uint8_t led_cmd_buffer[128] = { 0 };
    msg_fifo_init(&s_cmd_fifo, led_cmd_buffer, sizeof(led_cmd_buffer), sizeof(led_cmd_t));
    instance->cmd_fifo = &s_cmd_fifo;

    instance->is_static = true;
    instance->initialized = true;

    clist_add_tail(&s_led_head, &instance->node);
    return LED_OK;
}

led_error_t led_unregister(const char* name)
{
    if (name == NULL || !s_led_initialized)
        return LED_ERROR_INVALID_PARAM;

    led_handle_t *h, *tmp;
    clist_for_each_entry_safe(h, tmp, &s_led_head, node)
    {
        if (strcmp(h->config.name, name) == 0) {
            clist_del(&h->node);
            if (h->cmd_fifo)
                kfifo_reset(&h->cmd_fifo->fifo);
            return LED_OK;
        }
    }
    return LED_ERROR_NOT_FOUND;
}

void led_set_state(led_handle_t* instance, led_state_t state)
{
    if (instance == NULL)
        return;

    led_cmd_t cmd = { .led_set_state = state };
    msg_fifo_push(instance->cmd_fifo, &cmd);
}

led_error_t led_set_blink_interval(led_handle_t* instance,
    const led_cmd_t* cmd)
{
    if (instance == NULL || cmd == NULL)
        return LED_ERROR_INVALID_PARAM;

    bool changed = (instance->current_cmd.led_set_state != LED_STATE_BLINK_CODE)
        || (instance->current_cmd.led_blink_cycle_ms != cmd->led_blink_cycle_ms)
        || (instance->current_cmd.led_blink_wait_ms != cmd->led_blink_wait_ms)
        || (instance->current_cmd.led_blink_code_counts != cmd->led_blink_code_counts);
    if (!changed)
        return LED_OK;

    if (fsm_current_state(&instance->fsm) == LED_STATE_BLINK_CODE) {
        if (instance->blink_sw_on) {
            memcpy(&instance->current_cmd, cmd, sizeof(led_cmd_t));
            instance->current_cmd.led_set_state = LED_STATE_BLINK_CODE;
            instance->pending_blink_update = true;
            return msg_fifo_push(instance->cmd_fifo, &instance->current_cmd)
                ? LED_OK
                : LED_ERROR_INTERNAL;
        }
        memcpy(&instance->current_cmd, cmd, sizeof(led_cmd_t));
        instance->pending_blink_update = false;
        instance->current_led_blink_code_counts = 0;
        instance->blink_code_phase = LED_BLINK_PHASE_INTERVAL;
        instance->interval_start_time = s_led_get_time ? s_led_get_time() : 0;
        instance->blink_sw_on = false;
        led_phys_write(instance, 0);
        return LED_OK;
    }

    memcpy(&instance->current_cmd, cmd, sizeof(led_cmd_t));
    instance->pending_blink_update = false;
    return LED_OK;
}

led_blink_phase_t led_get_blink_phase(led_handle_t* instance)
{
    return instance ? (led_blink_phase_t)instance->blink_code_phase
                    : LED_BLINK_PHASE_INTERVAL;
}

void led_set_callbacks(led_handle_t* instance, led_state_change_cb_t state_cb,
    led_blink_phase_cb_t blink_phase_cb,
    led_edge_cb_t edge_cb, void* user_data)
{
    if (instance == NULL)
        return;
    instance->state_change_cb = (void*)state_cb;
    instance->blink_phase_cb = (void*)blink_phase_cb;
    instance->edge_cb = (void*)edge_cb;
    instance->callback_user_data = user_data;
}

void led_task_refresh(void)
{
    if (!s_led_initialized || clist_empty(&s_led_head))
        return;

    led_handle_t* h;
    clist_for_each_entry(h, &s_led_head, node)
    {
        if (!h->initialized)
            continue;
        led_process_cmds(h);
        fsm_step(&h->fsm);

        /* 检测闪烁阶段变化并触发回调 */
        if (h->blink_phase_cb && h->blink_code_phase != h->blink_code_phase_last) {
            ((led_blink_phase_cb_t)h->blink_phase_cb)(
                h, h->blink_code_phase, h->callback_user_data);
            h->blink_code_phase_last = h->blink_code_phase;
        }
    }
}
