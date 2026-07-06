
/**
 * @file    fsm.c
 * @author  maximilian
 * @version V1.0.0
 * @date    2026
 * @brief   通用有限状态机框架实现
 * @attention
 *
 * Copyright (c) 2026 Company Name.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 *
 *初始化示例：
  //栈上分配 handler 数组和转换扁平矩阵
  fsm_handler_t handlers[MOTOR_STATE_COUNT];
  fsm_guard_t transitions[MOTOR_STATE_COUNT * MOTOR_STATE_COUNT];

  // 清零所有数组
  memset(handlers, 0, sizeof(handlers));
  memset(transitions, 0, sizeof(transitions));

  // 填充 handler 表
  handlers[MOTOR_STATE_IDLE] = handle_idle;
  handlers[MOTOR_STATE_STARTING] = handle_starting;
  handlers[MOTOR_STATE_RUNNING] = handle_running;
  handlers[MOTOR_STATE_STOPPING] = handle_stopping;
  handlers[MOTOR_STATE_ERROR] = handle_error;

  // 构建配置结构体
  fsm_config_t config = {
      .handlers = handlers,
      .transitions = transitions,
      .state_count = MOTOR_STATE_COUNT,
      .entry_cb = on_entry_callback,
      .exit_cb = on_exit_callback,
      .state_names = state_names,
      .user_data = &motor,
  };

  // 一键填充全连通转换矩阵（所有状态之间可任意转换）
  fsm_fill(&config, fsm_always_true);

  // 个别边使用条件函数，在填充后单独覆盖
  *fsm_at(&config, MOTOR_STATE_IDLE, MOTOR_STATE_STARTING) = can_start;

  // 初始化状态机
  ret = fsm_init(&fsm, MOTOR_STATE_IDLE, &config);
 */

/* Includes ------------------------------------------------------------------*/
#include "fsm.h"

#include <string.h>

/* Private function prototypes -----------------------------------------------*/

/**
 * @brief 检查状态转换是否允许（邻接矩阵 O(1) 查询）
 * @param ctx 状态机上下文指针
 * @param target 目标状态
 * @return true表示转换有效，false表示无效
 */
static bool is_valid_transition(const fsm_t* ctx, fsm_state_t target);

/* Exported functions --------------------------------------------------------*/

fsm_err_t fsm_init(fsm_t* ctx, fsm_state_t initial_state,
    const fsm_config_t* config)
{
    // 检查参数有效性
    if (!ctx || !config) {
        return FSM_ERR_NULL;
    }

    if (!config->handlers || !config->transitions) {
        return FSM_ERR_NULL;
    }

    if (config->state_count == 0) {
        return FSM_ERR_STATE;
    }

    if (initial_state >= config->state_count) {
        return FSM_ERR_STATE;
    }

    // 清空整个上下文后写入配置（仅复制指针，不复制数组内容）
    memset(ctx, 0, sizeof(fsm_t));
    ctx->config = *config;

    // 初始化运行时状态
    ctx->current_state = initial_state;
    ctx->last_state = initial_state;
    ctx->initialized = true;

    return FSM_OK;
}

const char* fsm_name(const fsm_t* ctx, fsm_state_t state)
{
    if (ctx && ctx->config.state_names && state < ctx->config.state_count) {
        return ctx->config.state_names[state];
    }
    return "UNKNOWN";
}

fsm_err_t fsm_step(fsm_t* ctx)
{
    if (!ctx) {
        return FSM_ERR_NULL;
    }

    if (!ctx->initialized) {
        return FSM_ERR_INIT;
    }

    fsm_state_t current_state = ctx->current_state;
    fsm_state_t state_count = ctx->config.state_count;

    // 验证当前状态有效性
    if (current_state >= state_count) {
        return FSM_ERR_STATE;
    }

    // 状态发生变更时触发进入/退出回调
    if (ctx->last_state != current_state) {
        if (ctx->config.exit_cb) {
            ctx->config.exit_cb(ctx, ctx->last_state);
        }
        if (ctx->config.entry_cb) {
            ctx->config.entry_cb(ctx, current_state);
        }
        ctx->last_state = current_state;
    }

    // 执行当前状态处理器
    if (!ctx->config.handlers[current_state]) {
        return FSM_ERR_HANDLER;
    }

    fsm_state_t next_state = ctx->config.handlers[current_state](ctx);

    // 处理状态变更请求
    if (next_state != current_state) {
        if (is_valid_transition(ctx, next_state)) {
            ctx->current_state = next_state;
            return FSM_OK;
        }
        return FSM_ERR_TRANS;
    }

    return FSM_OK;
}

fsm_err_t fsm_goto(fsm_t* ctx, fsm_state_t target_state)
{
    if (!ctx) {
        return FSM_ERR_NULL;
    }

    if (!ctx->initialized) {
        return FSM_ERR_INIT;
    }

    if (target_state >= ctx->config.state_count) {
        return FSM_ERR_STATE;
    }

    // 验证转换合法性
    if (is_valid_transition(ctx, target_state)) {
        ctx->last_state = ctx->current_state;
        ctx->current_state = target_state;
        return FSM_OK;
    }

    return FSM_ERR_TRANS;
}

/* Config helpers -----------------------------------------------------------*/

void fsm_fill(fsm_config_t* config, fsm_guard_t condition)
{
    if (!config || !config->transitions || config->state_count == 0) {
        return;
    }

    // NULL 条件转为无条件哨兵
    if (condition == NULL) {
        condition = fsm_always_true;
    }

    uint8_t n = config->state_count;
    for (uint8_t from = 0; from < n; from++) {
        for (uint8_t to = 0; to < n; to++) {
            config->transitions[(uint16_t)from * n + to] = condition;
        }
    }
}

/* Private functions ---------------------------------------------------------*/

static bool is_valid_transition(const fsm_t* ctx, fsm_state_t target)
{
    if (!ctx) {
        return false;
    }

    uint8_t state_count = ctx->config.state_count;

    if (ctx->current_state >= state_count || target >= state_count) {
        return false;
    }

    // 允许保持当前状态
    if (ctx->current_state == target) {
        return true;
    }

    // O(1) 扁平矩阵查询
    fsm_guard_t* cond = fsm_at(&ctx->config, ctx->current_state, target);

    if (!cond) {
        return false;
    }

    if (*cond != NULL) {
        // 存在转换规则，执行条件判断（指向 fsm_always_true 表示无条件转换）
        return (*cond)(ctx);
    }

    // NULL 表示不存在转换规则
    return false;
}