//
// Created by fubingyan on 2025-09-22.
//

/**
 * @file    gimbal_pid.c
 * @author  fubingyan
 * @version V2.0.0
 * @date    2026-06-06
 * @brief   云台PID控制器实现 — 独立实例的 config/context 模式
 * @attention
 *
 * Copyright (c) 2025 by fubingyan, All Rights Reserved.
 */

/* Includes ------------------------------------------------------------------*/
#include "gimbal_pid.h"

#include <string.h>

#include "public.h"

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化云台PID实例
 * @param ctx 云台PID上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
gimbal_pid_error_t gimbal_pid_init(gimbal_pid_context_t* ctx,
    const gimbal_pid_config_t* config)
{
    if (!ctx || !config) {
        return GIMBAL_PID_ERROR_NULL_PTR;
    }

    // 如果已初始化，先反初始化
    if (ctx->initialized) {
        gimbal_pid_deinit(ctx);
    }

    // 保存配置
    memcpy(&ctx->config, config, sizeof(gimbal_pid_config_t));

    if (fabsf(ctx->config.ctrl_cycle_s) < 1e-6f) {
        ctx->config.ctrl_cycle_s = 1e-3f;
    }
    // 初始化运行时状态
    gimbal_pid_reset(ctx);

    ctx->initialized = true;

    return GIMBAL_PID_OK;
}

/**
 * @brief 反初始化云台PID实例
 * @param ctx 云台PID上下文指针
 */
void gimbal_pid_deinit(gimbal_pid_context_t* ctx)
{
    if (!ctx) {
        return;
    }

    gimbal_pid_reset(ctx);
    memset(&ctx->config, 0, sizeof(gimbal_pid_config_t));
    ctx->initialized = false;
}

/**
 * @brief 检查云台PID实例是否已初始化
 * @param ctx 云台PID上下文指针
 * @return true表示已初始化，false表示未初始化
 */
bool gimbal_pid_is_initialized(const gimbal_pid_context_t* ctx)
{
    return (ctx && ctx->initialized);
}

/**
 * @brief 更新云台PID反馈数据
 * @param ctx 云台PID上下文指针
 * @param update 反馈更新结构体指针
 */
void gimbal_pid_update_feedback(gimbal_pid_context_t* ctx,
    const gimbal_pid_update_t* update)
{
    if (!ctx || !update) {
        return;
    }

    ctx->get = update->get;
    ctx->set = update->set;
    ctx->derivative = update->gyro;
    ctx->error_delta = update->error_delta;
}

/**
 * @brief 计算云台PID输出
 * @param ctx 云台PID上下文指针
 * @return PID输出值
 * @note 每次调用内部计数器+1，达到 config.control_cycle 时执行实际PID计算
 */
float gimbal_pid_calc(gimbal_pid_context_t* ctx)
{
    if (!ctx || !ctx->initialized) {
        return 0.0f;
    }

    // 计算误差并约束到 [-π, π)
    float err = ctx->set - ctx->get;
    utils_norm_angle_rad(&err);
    ctx->err = err;

    // 比例项
    ctx->p_out = ctx->config.kp * ctx->err;

    // 死区积分，避免在接近目标值时积分饱和
    ctx->i_out += ctx->config.ki * ctx->err * ctx->config.ctrl_cycle_s;

    // 微分项（使用 error_delta）
    ctx->d_out = ctx->config.kd * ctx->error_delta;

    // 积分限幅
    utils_truncate_number_abs(&ctx->i_out, ctx->config.max_iout);

    // 合成总输出
    ctx->out = ctx->p_out + ctx->i_out + ctx->d_out;

    // 输出限幅
    utils_truncate_number_abs(&ctx->out, ctx->config.max_out);

    return ctx->out;
}

/**
 * @brief 获取云台PID当前输出值
 * @param ctx 云台PID上下文指针
 * @return 当前PID输出值
 */
float gimbal_pid_get_output(const gimbal_pid_context_t* ctx)
{
    if (!ctx || !ctx->initialized) {
        return 0.0f;
    }
    return ctx->out;
}

/**
 * @brief 重置云台PID内部状态
 * @param ctx 云台PID上下文指针
 */
void gimbal_pid_reset(gimbal_pid_context_t* ctx)
{
    if (!ctx) {
        return;
    }

    ctx->err = 0.0f;
    ctx->set = 0.0f;
    ctx->get = 0.0f;
    ctx->out = 0.0f;
    ctx->p_out = 0.0f;
    ctx->i_out = 0.0f;
    ctx->d_out = 0.0f;
    ctx->derivative = 0.0f;
    ctx->error_delta = 0.0f;
}
