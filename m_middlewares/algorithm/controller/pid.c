//
// Created by fubingyan on 2025-07-09.
//

/**
 * @file    pid.c
 * @author  fubingyan
 * @version V2.0.0
 * @date    2026-06-06
 * @brief   通用PID控制器实现 — 独立实例的 config/context 模式
 * @attention
 *
 * Copyright (c) 2025 by fubingyan, All Rights Reserved.
 */

/* Includes ------------------------------------------------------------------*/
#include "pid.h"

#include <string.h>

#include "public.h"

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化PID实例
 * @param ctx PID上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
pid_error_t pid_init(pid_context_t* ctx, const pid_config_t* config)
{
    if (!ctx || !config) {
        return PID_ERROR_NULL_PTR;
    }

    // 如果已初始化，先反初始化
    if (ctx->initialized) {
        pid_deinit(ctx);
    }

    // 保存配置
    memcpy(&ctx->config, config, sizeof(pid_config_t));

    if (fabsf(ctx->config.ctrl_cycle_s) < 1e-6f) {
        ctx->config.ctrl_cycle_s = 1e-3f;
    }
    // 清零所有运行时状态
    pid_reset(ctx);

    ctx->initialized = true;

    return PID_OK;
}

/**
 * @brief 反初始化PID实例
 * @param ctx PID上下文指针
 */
void pid_deinit(pid_context_t* ctx)
{
    if (!ctx) {
        return;
    }

    pid_reset(ctx);
    memset(&ctx->config, 0, sizeof(pid_config_t));
    ctx->initialized = false;
}

/**
 * @brief 检查PID实例是否已初始化
 * @param ctx PID上下文指针
 * @return true表示已初始化，false表示未初始化
 */
bool pid_is_initialized(const pid_context_t* ctx)
{
    return (ctx && ctx->initialized);
}

/**
 * @brief 计算PID输出
 * @param ctx PID上下文指针
 * @param ref 反馈值
 * @param set 设定值
 * @return PID输出值
 */
float pid_calc(pid_context_t* ctx, float ref, float set)
{
    if (!ctx || !ctx->initialized) {
        return 0.0f;
    }

    // 更新误差历史
    ctx->error[2] = ctx->error[1];
    ctx->error[1] = ctx->error[0];
    ctx->set = set;
    ctx->fdb = ref;
    ctx->error[0] = set - ref;

    if (ctx->config.mode == PID_MODE_POSITION) {
        // 位置式PID
        ctx->p_out = ctx->config.kp * ctx->error[0];
        ctx->i_out += ctx->config.ki * ctx->error[0];

        ctx->dbuf[2] = ctx->dbuf[1];
        ctx->dbuf[1] = ctx->dbuf[0];
        ctx->dbuf[0] = ctx->error[0] - ctx->error[1];
        ctx->d_out = ctx->config.kd * ctx->dbuf[0];

        // 积分限幅
        utils_truncate_number_abs(&ctx->i_out, ctx->config.max_iout);

        // 合成输出（含前馈）
        ctx->out = ctx->p_out + ctx->i_out + ctx->d_out;

        // 输出限幅
        utils_truncate_number_abs(&ctx->out, ctx->config.max_out);
    } else {
        // 增量式PID
        ctx->p_out = ctx->config.kp * (ctx->error[0] - ctx->error[1]);
        ctx->i_out = ctx->config.ki * ctx->error[0];

        ctx->dbuf[2] = ctx->dbuf[1];
        ctx->dbuf[1] = ctx->dbuf[0];
        ctx->dbuf[0] = ctx->error[0] - 2.0f * ctx->error[1] + ctx->error[2];
        ctx->d_out = ctx->config.kd * ctx->dbuf[0];

        ctx->out += ctx->p_out + ctx->i_out + ctx->d_out;

        // 输出限幅
        utils_truncate_number_abs(&ctx->out, ctx->config.max_out);
    }

    return ctx->out;
}

/**
 * @brief 重置PID内部状态（清零积分/微分历史）
 * @param ctx PID上下文指针
 */
void pid_reset(pid_context_t* ctx)
{
    if (!ctx) {
        return;
    }

    ctx->error[0] = 0.0f;
    ctx->error[1] = 0.0f;
    ctx->error[2] = 0.0f;
    ctx->dbuf[0] = 0.0f;
    ctx->dbuf[1] = 0.0f;
    ctx->dbuf[2] = 0.0f;
    ctx->out = 0.0f;
    ctx->p_out = 0.0f;
    ctx->i_out = 0.0f;
    ctx->d_out = 0.0f;
    ctx->fdb = 0.0f;
    ctx->set = 0.0f;
}

pid_error_t pid_set_config(pid_context_t* ctx, const pid_config_t* config)
{
    if (!ctx || !config) {
        return PID_ERROR_NULL_PTR;
    }

    memcpy(&ctx->config, config, sizeof(pid_config_t));

    if (fabsf(ctx->config.ctrl_cycle_s) < 1e-6f) {
        ctx->config.ctrl_cycle_s = 1e-3f;
    }

    ctx->initialized = true;
    return PID_OK;
}
