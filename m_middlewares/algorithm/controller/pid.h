//
// Created by fubingyan on 2025-07-09.
//

/**
 * @file    pid.h
 * @author  fubingyan
 * @version V2.0.0
 * @date    2026-06-06
 * @brief   通用PID控制器 — 独立实例的 config/context 模式
 * @attention
 *
 * Copyright (c) 2025 by fubingyan, All Rights Reserved.
 */

#ifndef __PID_H
#define __PID_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief PID错误码枚举
 */
typedef enum {
    PID_OK = 0, /**< 操作成功 */
    PID_ERROR_NULL_PTR, /**< 空指针错误 */
    PID_ERROR_UNINITIALIZED, /**< 未初始化 */
} pid_error_t;

/**
 * @brief PID模式枚举
 */
typedef enum {
    PID_MODE_POSITION = 0, /**< 位置式PID */
    PID_MODE_DELTA, /**< 增量式PID */
} pid_mode_t;

/**
 * @brief PID配置结构体
 */
typedef struct {
    pid_mode_t mode; /**< PID模式 */
    float ctrl_cycle_s; /** 控制周期 单位：秒 */
    float kp; /**< 比例系数 */
    float ki; /**< 积分系数 */
    float kd; /**< 微分系数 */
    float max_out; /**< 最大输出限制 */
    float max_iout; /**< 最大积分输出限制 */
} pid_config_t;

/* 前向声明 */
typedef struct pid_context pid_context_t;

/**
 * @brief PID上下文结构体
 */
struct pid_context {
    pid_config_t config; /**< 配置参数 */

    /* 运行时状态 */
    float set; /**< 设定值 */
    float fdb; /**< 反馈值 */
    float out; /**< 总输出 */
    float p_out; /**< 比例项输出 */
    float i_out; /**< 积分项输出 */
    float d_out; /**< 微分项输出 */
    float dbuf[3]; /**< 微分项缓冲区 [最新, 上一次, 上上次] */
    float error[3]; /**< 误差项缓冲区 [最新, 上一次, 上上次] */

    bool initialized; /**< 初始化标志 */
};

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化PID实例
 * @param ctx PID上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
pid_error_t pid_init(pid_context_t* ctx, const pid_config_t* config);

/**
 * @brief 反初始化PID实例
 * @param ctx PID上下文指针
 */
void pid_deinit(pid_context_t* ctx);

/**
 * @brief 检查PID实例是否已初始化
 * @param ctx PID上下文指针
 * @return true表示已初始化，false表示未初始化
 */
bool pid_is_initialized(const pid_context_t* ctx);

/**
 * @brief 计算PID输出
 * @param ctx PID上下文指针
 * @param ref 反馈值
 * @param set 设定值
 * @return PID输出值
 */
float pid_calc(pid_context_t* ctx, float ref, float set);

/**
 * @brief 重置PID内部状态（清零积分/微分历史）
 * @param ctx PID上下文指针
 */
void pid_reset(pid_context_t* ctx);

/**
 * @brief 更新PID配置参数（不清零运行时状态）
 * @param ctx PID上下文指针
 * @param config 新配置结构体指针
 * @return 操作结果错误码
 */
pid_error_t pid_set_config(pid_context_t* ctx, const pid_config_t* config);

#ifdef __cplusplus
}
#endif

#endif /* __PID_H */
