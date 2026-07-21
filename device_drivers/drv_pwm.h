/**
 * @file    drv_pwm.h
 * @author  maximillian
 * @version V1.0.0
 * @date    2026-07-20
 * @brief   PWM 设备驱动（TIM 外设抽象，多通道）
 * @attention
 *
 * STM32G474RBTx 通用定时器 PWM 输出驱动。
 * 当前使用 TIM5_CH1 (PA0)，可扩展至多通道。
 * 时钟配置（Prescaler / Period）由 CubeMX 生成，本驱动不修改。
 */

#ifndef __DRV_PWM_H
#define __DRV_PWM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief PWM 通道枚举
 */
typedef enum {
    DRV_PWM_CH_1 = 0, /**< TIM5_CH1 — PA0 */
    DRV_PWM_CH_NUM,   /**< 通道总数 */
} drv_pwm_channel_t;

/**
 * @brief 驱动错误码
 */
typedef enum {
    DRV_PWM_OK = 0,
    DRV_PWM_ERROR_NULL_PTR,
    DRV_PWM_ERROR_UNINITIALIZED,
    DRV_PWM_ERROR_INVALID_PARAM,
} drv_pwm_error_t;

/* Exported functions prototypes ---------------------------------------------*/

/* --- 初始化 / 生命周期 --- */

/**
 * @brief 初始化 PWM 通道
 * @param ch      通道号
 * @param htim    HAL 定时器句柄 (TIM_HandleTypeDef*)，CubeMX 生成的 &htim5 等
 * @param channel HAL 通道号 (如 TIM_CHANNEL_1)
 * @return 操作结果错误码
 */
drv_pwm_error_t drv_pwm_init(drv_pwm_channel_t ch, void* htim, uint32_t channel);
void drv_pwm_deinit(drv_pwm_channel_t ch);
bool drv_pwm_is_initialized(drv_pwm_channel_t ch);

/* --- 占空比控制 --- */

/**
 * @brief 设置 PWM 占空比
 * @param ch   通道号
 * @param duty 比较值（0 ~ ARR，由 CubeMX Period 决定；TIM5 为 0-1023）
 * @return 操作结果错误码
 */
drv_pwm_error_t drv_pwm_set_duty(drv_pwm_channel_t ch, uint32_t duty);

/* --- 运行控制 --- */

/**
 * @brief 启动 PWM 输出
 * @param ch 通道号
 * @return 操作结果错误码
 */
drv_pwm_error_t drv_pwm_start(drv_pwm_channel_t ch);

/**
 * @brief 停止 PWM 输出
 * @param ch 通道号
 * @return 操作结果错误码
 */
drv_pwm_error_t drv_pwm_stop(drv_pwm_channel_t ch);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_PWM_H */
