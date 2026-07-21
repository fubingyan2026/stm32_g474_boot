//
// Created by fubingyan on 25-9-20.
//

/**
 * @file    led.h
 * @brief   LED 控制模块 — 支持 ON/OFF/编码闪烁/呼吸四种状态
 * @note    使用 FSM 管理状态转换，kfifo 异步命令队列接收外部指令。
 *          引脚操作通过 config 回调解耦（uint16_t PWM 接口），不直接依赖 HAL。
 *          实例链表使用侵入式 clist 管理。
 */

#ifndef __LED_H
#define __LED_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "clist.h"
#include "fsm.h"
#include "msg_fifo.h"

/* Exported types ------------------------------------------------------------*/

/**
 * @brief LED 错误码
 */
typedef enum {
    LED_OK = 0,                 /**< 操作成功 */
    LED_OK_EXISTED = 1,         /**< 已初始化 */
    LED_ERROR_INVALID_PARAM = -1, /**< 无效参数 */
    LED_ERROR_NO_MEMORY = -2,   /**< 内存不足 */
    LED_ERROR_NOT_FOUND = -3,   /**< 未找到实例 */
    LED_ERROR_ALREADY_EXIST = -4,/**< 同名实例已存在 */
    LED_ERROR_INTERNAL = -5,    /**< 内部错误 */
} led_error_t;

/**
 * @brief LED 工作状态
 */
typedef enum __attribute__((packed)) {
    LED_STATE_NONE = 0,         /**< 无状态（空闲） */
    LED_STATE_OFF,              /**< 熄灭 */
    LED_STATE_ON,               /**< 常亮 */
    LED_STATE_BLINK_CODE,       /**< 编码闪烁 */
    LED_STATE_BREATHING,        /**< 呼吸灯 */
    LED_STATE_MAX,              /**< 状态总数 */
} led_state_t;

/**
 * @brief 闪烁阶段
 */
typedef enum __attribute__((packed)) {
    LED_BLINK_PHASE_BLINKING = 0, /**< 闪烁中：按 cycle_ms 切换亮灭 */
    LED_BLINK_PHASE_INTERVAL,   /**< 间隔中：按 wait_ms 保持熄灭 */
} led_blink_phase_t;

/**
 * @brief LED 句柄前向声明
 */
typedef struct led_handle led_handle_t;

/**
 * @brief 系统时间获取回调
 * @return 毫秒时间戳
 */
typedef uint32_t (*led_get_time_cb_t)(void);

/**
 * @brief LED 状态变化回调
 * @param instance 触发回调的 LED 实例
 * @param new_state 新状态
 * @param user_data 用户数据
 */
typedef void (*led_state_change_cb_t)(led_handle_t* instance,
    led_state_t new_state, void* user_data);

/**
 * @brief 闪烁阶段变化回调
 * @param instance LED 实例
 * @param phase 当前闪烁阶段
 * @param user_data 用户数据
 */
typedef void (*led_blink_phase_cb_t)(led_handle_t* instance,
    led_blink_phase_t phase, void* user_data);

/**
 * @brief GPIO 边沿变化回调
 * @param instance LED 实例
 * @param rising true=上升沿(亮)，false=下降沿(灭)
 * @param user_data 用户数据
 */
typedef void (*led_edge_cb_t)(led_handle_t* instance, bool rising,
    void* user_data);

/**
 * @brief LED 呼吸默认参数
 */
#define LED_BREATH_CYCLE_MS_DEFAULT  (2000U)
#define LED_BREATH_STEP_MS_DEFAULT   (30U)
#define LED_BREATH_MIN_DUTY_DEFAULT  (0U)
#define LED_BREATH_MAX_DUTY_DEFAULT  (1024U)

/**
 * @brief LED 配置
 * @note write_pin 由用户实现，负责实际引脚 PWM 操作
 */
typedef struct {
    const char* name;               /**< LED 名称（唯一标识） */
    led_state_t init_state;         /**< 初始状态 */
    void (*write_pin)(uint16_t value); /**< 引脚写入：0=灭, 1023=最亮, 中间=呼吸 */

    /* 呼吸默认参数（0 表示使用默认值） */
    uint16_t breath_cycle_ms;       /**< 呼吸周期(ms), 0=2000 */
    uint16_t breath_step_ms;        /**< 步进间隔(ms), 0=30 */
    uint16_t breath_min_duty;       /**< 最小亮度(0-1023), 0=0 */
    uint16_t breath_max_duty;       /**< 最大亮度(0-1023), 0=1023 */
} led_config_t;

/**
 * @brief LED 异步命令
 */
typedef struct {
    led_state_t led_set_state;          /**< 目标状态 */
    uint16_t led_blink_cycle_ms;        /**< 闪烁间隔(ms) */
    uint16_t led_blink_wait_ms;         /**< 等待间隔(ms) */
    uint16_t led_blink_code_counts;     /**< 闪烁次数（0=无限循环） */

    /* 呼吸参数（运行时修改） */
    uint16_t led_breath_cycle_ms;       /**< 呼吸周期(ms), 0=不变 */
    uint16_t led_breath_min_duty;       /**< 最小亮度, 0xFFFF=不变 */
    uint16_t led_breath_max_duty;       /**< 最大亮度, 0xFFFF=不变 */
} led_cmd_t;

/**
 * @brief LED 控制句柄
 */
struct led_handle {
    led_config_t config;            /**< 配置副本 */
    clist_head_t node;              /**< clist 链表节点 */
    fsm_t fsm;                      /**< FSM 状态机上下文 */

    led_cmd_t current_cmd;          /**< 当前命令参数 */
    uint32_t last_toggle_time;      /**< 上次翻转时间戳 */
    uint32_t interval_start_time;   /**< 间隔阶段起始时间戳 */

    uint16_t current_led_blink_code_counts; /**< 当前闪烁计数 */
    led_blink_phase_t blink_code_phase;     /**< 当前闪烁阶段 */
    led_blink_phase_t blink_code_phase_last;/**< 上次闪烁阶段 */
    bool blink_sw_on;               /**< 软件跟踪 LED 亮灭状态 */

    /* 呼吸状态 */
    uint32_t last_breath_time;      /**< 上次呼吸步进时间戳 */
    uint16_t breath_cycle;          /**< 呼吸步进计数器 */
    uint16_t breath_step_ms;        /**< 步进间隔(ms) */
    uint16_t breath_cycle_ms;       /**< 呼吸周期(ms) */
    uint16_t breath_min_duty;       /**< 最小亮度(0-1023) */
    uint16_t breath_max_duty;       /**< 最大亮度(0-1023) */
    uint16_t breath_value;          /**< 当前计算出的亮度值 */

    uint16_t last_write_value;      /**< 上次写入引脚的 PWM 值（用于状态过渡） */

    bool is_static;                 /**< 静态分配标志 */
    bool initialized;               /**< 初始化完成标志 */
    bool pending_blink_update;      /**< 待处理的闪烁参数更新 */

    msg_fifo_t* cmd_fifo;           /**< 异步命令队列 */

    void* state_change_cb;          /**< 状态变化回调 */
    void* blink_phase_cb;           /**< 闪烁阶段变化回调 */
    void* edge_cb;                  /**< 引脚边沿回调 */
    void* callback_user_data;       /**< 回调用户数据 */
};

/* Exported constants --------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

#define LED_IS_OK(err) ((err) >= 0)
#define LED_IS_ERR(err) ((err) < 0)

/* Exported functions prototypes ---------------------------------------------*/

led_error_t led_init(led_get_time_cb_t get_time_cb);
void led_deinit(void);

led_error_t led_register_static(const led_config_t* config,
    led_handle_t* instance);
led_error_t led_unregister(const char* name);
led_handle_t* led_get_instance(const char* name);
clist_head_t* led_get_head(void);

void led_set_state(led_handle_t* instance, led_state_t state);
led_error_t led_set_blink_interval(led_handle_t* instance,
    const led_cmd_t* cmd);
led_blink_phase_t led_get_blink_phase(led_handle_t* instance);

void led_set_callbacks(led_handle_t* instance, led_state_change_cb_t state_cb,
    led_blink_phase_cb_t blink_phase_cb,
    led_edge_cb_t edge_cb, void* user_data);

void led_task_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED_H */
