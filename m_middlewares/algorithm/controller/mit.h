//
// Created by fubingyan on 2026-06-28.
//

/**
 * @file    mit.h
 * @author  fubingyan
 * @version V1.0.0
 * @date    2026-06-28
 * @brief   MIT控制模式 — PD + 前馈（阻抗控制）
 * @attention
 *
 * 公式：τ = Kp × (pos_set − pos_fdb) + Kd × (vel_set − vel_fdb) + ff
 *       输出 = 位置刚度项 + 速度阻尼项 + 前馈力矩
 *
 * 典型应用：腿足机器人关节控制、阻抗/导纳控制
 *
 * Copyright (c) 2026 by fubingyan, All Rights Reserved.
 */

#ifndef __MIT_H
#define __MIT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief MIT控制器错误码枚举
 */
typedef enum {
    MIT_OK = 0, /**< 操作成功 */
    MIT_ERROR_NULL_PTR, /**< 空指针错误 */
    MIT_ERROR_UNINITIALIZED, /**< 未初始化 */
} mit_error_t;

/**
 * @brief MIT控制器配置结构体
 */
typedef struct {
    float kp; /**< 位置刚度系数
                   @note kp 越大，位置跟踪越刚硬 */
    float kd; /**< 速度阻尼系数
                   @note kd 越大，运动阻尼越大，系统越稳定 */
    float max_out; /**< 最大输出限幅（绝对值）
                        @note 限制最终输出的绝对值不超过此值 */
} mit_config_t;

/* 前向声明 */
typedef struct mit_context mit_context_t;

/**
 * @brief MIT控制器上下文结构体
 */
struct mit_context {
    mit_config_t config; /**< 配置参数 */

    /* 运行时状态 */
    float pos_set; /**< 位置设定值 */
    float vel_set; /**< 速度设定值 */
    float pos_fdb; /**< 位置反馈值 */
    float vel_fdb; /**< 速度反馈值 */
    float ff; /**< 前馈力矩 */
    float out; /**< 总输出 */
    float p_out; /**< 比例项输出（位置刚度项） */
    float d_out; /**< 微分项输出（速度阻尼项） */

    bool initialized; /**< 初始化标志 */
};

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化MIT控制器实例
 * @param ctx MIT上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
mit_error_t mit_init(mit_context_t* ctx, const mit_config_t* config);

/**
 * @brief 反初始化MIT控制器实例
 * @param ctx MIT上下文指针
 */
void mit_deinit(mit_context_t* ctx);

/**
 * @brief 检查MIT控制器实例是否已初始化
 * @param ctx MIT上下文指针
 * @return true表示已初始化，false表示未初始化
 */
bool mit_is_initialized(const mit_context_t* ctx);

/**
 * @brief 计算MIT控制器输出
 * @param ctx MIT上下文指针
 * @param pos_set 位置设定值
 * @param vel_set 速度设定值（通常为 0 或来自轨迹规划）
 * @param pos_fdb 位置反馈值（编码器角度）
 * @param vel_fdb 速度反馈值（速度估计）
 * @param ff 前馈力矩值
 * @return MIT输出值（单位与 kp/kd 一致）
 *
 * @note 公式: out = Kp × (pos_set − pos_fdb) + Kd × (vel_set − vel_fdb) + ff
 *       输出受 config.max_out 限幅保护
 */
float mit_calc(mit_context_t* ctx, float pos_set, float vel_set,
    float pos_fdb, float vel_fdb, float ff);

/**
 * @brief 重置MIT控制器内部状态
 * @param ctx MIT上下文指针
 * @note 清零所有运行时状态，保留配置参数和初始化标志
 */
void mit_reset(mit_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* __MIT_H */
