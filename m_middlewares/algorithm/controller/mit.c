//
// Created by fubingyan on 2026-06-28.
//

/**
 * @file    mit.c
 * @author  fubingyan
 * @version V1.0.0
 * @date    2026-06-28
 * @brief   MIT控制模式实现 — PD + 前馈（阻抗控制）
 * @attention
 *
 * Copyright (c) 2026 by fubingyan, All Rights Reserved.
 */

/* Includes ------------------------------------------------------------------*/
#include "mit.h"

#include <string.h>

#include "public.h"

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化MIT控制器实例
 * @param ctx MIT上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
mit_error_t mit_init(mit_context_t* ctx, const mit_config_t* config)
{
    if (!ctx || !config) {
        return MIT_ERROR_NULL_PTR;
    }

    // 如果已初始化，先反初始化
    if (ctx->initialized) {
        mit_deinit(ctx);
    }

    // 保存配置
    memcpy(&ctx->config, config, sizeof(mit_config_t));

    // 清零所有运行时状态
    mit_reset(ctx);

    ctx->initialized = true;

    return MIT_OK;
}

/**
 * @brief 反初始化MIT控制器实例
 * @param ctx MIT上下文指针
 */
void mit_deinit(mit_context_t* ctx)
{
    if (!ctx) {
        return;
    }

    mit_reset(ctx);
    memset(&ctx->config, 0, sizeof(mit_config_t));
    ctx->initialized = false;
}

/**
 * @brief 检查MIT控制器实例是否已初始化
 * @param ctx MIT上下文指针
 * @return true表示已初始化，false表示未初始化
 */
bool mit_is_initialized(const mit_context_t* ctx)
{
    return (ctx && ctx->initialized);
}

/**
 * @brief 计算MIT控制器输出
 * @param ctx MIT上下文指针
 * @param pos_set 位置设定值
 * @param vel_set 速度设定值
 * @param pos_fdb 位置反馈值
 * @param vel_fdb 速度反馈值
 * @param ff 前馈力矩值
 * @return MIT输出值
 *
 * @note 公式: τ = Kp × (pos_set − pos_fdb) + Kd × (vel_set − vel_fdb) + ff
 *       输出受 config.max_out 限幅保护
 */
float mit_calc(mit_context_t* ctx, float pos_set, float vel_set,
    float pos_fdb, float vel_fdb, float ff)
{
    if (!ctx || !ctx->initialized) {
        return 0.0f;
    }

    // 保存输入
    ctx->pos_set = pos_set;
    ctx->vel_set = vel_set;
    ctx->pos_fdb = pos_fdb;
    ctx->vel_fdb = vel_fdb;
    ctx->ff = ff;

    // 位置刚度项
    float pos_err = pos_set - pos_fdb;
    ctx->p_out = ctx->config.kp * pos_err;

    // 速度阻尼项
    float vel_err = vel_set - vel_fdb;
    ctx->d_out = ctx->config.kd * vel_err;

    // 合成输出（含前馈）
    ctx->out = ctx->p_out + ctx->d_out + ff;

    // 输出限幅
    utils_truncate_number_abs(&ctx->out, ctx->config.max_out);

    return ctx->out;
}

/**
 * @brief 重置MIT控制器内部状态（清零所有运行时变量）
 * @param ctx MIT上下文指针
 */
void mit_reset(mit_context_t* ctx)
{
    if (!ctx) {
        return;
    }

    ctx->pos_set = 0.0f;
    ctx->vel_set = 0.0f;
    ctx->pos_fdb = 0.0f;
    ctx->vel_fdb = 0.0f;
    ctx->ff = 0.0f;
    ctx->out = 0.0f;
    ctx->p_out = 0.0f;
    ctx->d_out = 0.0f;
}
