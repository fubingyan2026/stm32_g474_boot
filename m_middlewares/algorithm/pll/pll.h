//
// Created by fubingyan on 26-5-19.
//

#ifndef __PLL_H
#define __PLL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "filter.h"

/* Exported types ------------------------------------------------------------*/

/**
 * @brief PLL 错误码枚举
 */
typedef enum {
    PLL_OK = 0,                   /**< 操作成功 */
    PLL_ERROR_NULL_PTR,           /**< 空指针错误 */
    PLL_ERROR_UNINITIALIZED,      /**< 未初始化 */
    PLL_ERROR_ALREADY_INITIALIZED,/**< 已初始化 */
    PLL_ERROR_INVALID_PARAM,      /**< 无效参数 */
} pll_error_t;

/**
 * @brief PLL 配置结构体
 */
typedef struct {
    float kp;                     /**< PI 控制器比例增益 */
    float ki;                     /**< PI 控制器积分增益 */
    float sample_time;            /**< 采样时间 (秒) */
    float filter_freq_omega;      /**< 角速度输出低通滤波截止频率 (Hz)，0 表示不使用滤波 */
} pll_config_t;

/**
 * @brief PLL 上下文结构体
 */
typedef struct pll_context pll_context_t;

struct pll_context {
    pll_config_t config;          /**< 配置参数 */

    // PLL 状态
    float theta;                  /**< 估计角度 [rad] */
    float omega;                  /**< 估计角速度 [rad/s] */
    float intg_pll;               /**< PI 积分器 */

    // PT1 滤波器（可选，由配置中的 filter_freq 决定）
    pt1Filter_t filter_omega;     /**< 角速度低通滤波器 */

    bool startup_done;            /**< 启动阶段完成标志（atan2 初始化后置 true） */
    bool initialized;             /**< 初始化标志 */
};

/* Exported constants --------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化 PLL
 * @param ctx PLL 上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
pll_error_t pll_init(pll_context_t *ctx, const pll_config_t *config);

/**
 * @brief 反初始化 PLL
 * @param ctx PLL 上下文指针
 */
void pll_deinit(pll_context_t *ctx);

/**
 * @brief 检查 PLL 是否已初始化
 * @param ctx PLL 上下文指针
 * @return true 表示已初始化，false 表示未初始化
 */
bool pll_is_initialized(const pll_context_t *ctx);

/**
 * @brief 更新 PLL（输入两路正交信号）
 * @param ctx PLL 上下文指针
 * @param signal_a 通道 A 信号值（归一化到 [-1, 1]）
 * @param signal_b 通道 B 信号值（归一化到 [-1, 1]）
 * @return 操作结果错误码
 */
pll_error_t pll_update(pll_context_t *ctx, float signal_a, float signal_b);

/**
 * @brief 获取估计角度
 * @param ctx PLL 上下文指针
 * @return 估计角度 [rad]，范围 [-PI, PI]
 */
float pll_get_angle(const pll_context_t *ctx);

/**
 * @brief 获取估计角速度
 * @param ctx PLL 上下文指针
 * @return 估计角速度 [rad/s]
 */
float pll_get_speed(const pll_context_t *ctx);

/**
 * @brief 重置 PLL 状态（保持配置不变）
 * @param ctx PLL 上下文指针
 * @return 操作结果错误码
 */
pll_error_t pll_reset(pll_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* __PLL_H */
