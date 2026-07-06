//
// Created by fubingyan on 2025-09-22.
//

/**
 * @file    gimbal_pid.h
 * @author  fubingyan
 * @version V2.0.0
 * @date    2026-06-06
 * @brief   云台PID控制器 — 独立实例的 config/context 模式
 * @attention
 *
 * Copyright (c) 2025 by fubingyan, All Rights Reserved.
 */

#ifndef __GIMBAL_PID_H
#define __GIMBAL_PID_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 云台PID错误码枚举
 */
typedef enum {
    GIMBAL_PID_OK = 0, /**< 操作成功 */
    GIMBAL_PID_ERROR_NULL_PTR, /**< 空指针错误 */
    GIMBAL_PID_ERROR_UNINITIALIZED, /**< 未初始化 */
} gimbal_pid_error_t;

/**
 * @brief 云台PID配置结构体
 */
typedef struct {
    float ctrl_cycle_s; /** 控制周期 单位：秒 */
    float kp; /**< 比例系数 */
    float ki; /**< 积分系数 */
    float kd; /**< 微分系数 */
    float max_out; /**< 最大输出限制 */
    float max_iout; /**< 最大积分输出限制 */

} gimbal_pid_config_t;

/**
 * @brief 云台PID反馈更新结构体
 */
typedef struct {
    float set; /**< 设定值 */
    float get; /**< 反馈值 */
    float gyro; /**< 陀螺仪角速度 */
    float error_delta; /**< 误差变化量 */
} gimbal_pid_update_t;

/* 前向声明 */
typedef struct gimbal_pid_context gimbal_pid_context_t;

/**
 * @brief 云台PID上下文结构体
 */
struct gimbal_pid_context {
    gimbal_pid_config_t config; /**< 配置参数 */

    /* 反馈数据 */
    float set; /**< 设定值 */
    float get; /**< 反馈值 */
    float error_delta; /**< 误差变化量 */
    float derivative; /**< 微分项原始值（陀螺仪角速度） */

    /* PID 中间变量 */
    float err; /**< 当前误差 */
    float p_out; /**< 比例项输出 */
    float i_out; /**< 积分项输出 */
    float d_out; /**< 微分项输出 */
    float out; /**< 总输出 */

    bool initialized; /**< 初始化标志 */
};

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化云台PID实例
 * @param ctx 云台PID上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
gimbal_pid_error_t gimbal_pid_init(gimbal_pid_context_t* ctx,
    const gimbal_pid_config_t* config);

/**
 * @brief 反初始化云台PID实例
 * @param ctx 云台PID上下文指针
 */
void gimbal_pid_deinit(gimbal_pid_context_t* ctx);

/**
 * @brief 检查云台PID实例是否已初始化
 * @param ctx 云台PID上下文指针
 * @return true表示已初始化，false表示未初始化
 */
bool gimbal_pid_is_initialized(const gimbal_pid_context_t* ctx);

/**
 * @brief 更新云台PID反馈数据
 * @param ctx 云台PID上下文指针
 * @param update 反馈更新结构体指针
 */
void gimbal_pid_update_feedback(gimbal_pid_context_t* ctx,
    const gimbal_pid_update_t* update);

/**
 * @brief 计算云台PID输出
 * @param ctx 云台PID上下文指针
 * @return PID输出值
 * @note 每次调用内部计数器+1，达到 config.control_cycle 时执行实际PID计算。
 *       调用者需保证以固定频率调用此函数。
 */
float gimbal_pid_calc(gimbal_pid_context_t* ctx);

/**
 * @brief 获取云台PID当前输出值
 * @param ctx 云台PID上下文指针
 * @return 当前PID输出值，未初始化返回0
 */
float gimbal_pid_get_output(const gimbal_pid_context_t* ctx);

/**
 * @brief 重置云台PID内部状态
 * @param ctx 云台PID上下文指针
 * @note 将PID所有中间变量清零，不移除配置
 */
void gimbal_pid_reset(gimbal_pid_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* __GIMBAL_PID_H */
