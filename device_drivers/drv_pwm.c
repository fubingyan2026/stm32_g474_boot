/**
 * @file    drv_pwm.c
 * @author  maximillian
 * @version V1.1.0
 * @date    2026-07-20
 * @brief   PWM 设备驱动实现（TIM 外设抽象，多通道，直接寄存器操作）
 * @attention
 *
 * 不依赖 HAL 状态机（ChannelState），直接操作 TIM 寄存器以保证可靠性。
 * 时钟/GPIO 配置仍由 CubeMX 生成的 HAL 代码完成（MX_TIM5_Init）。
 */

/* Includes ------------------------------------------------------------------*/
#include "drv_pwm.h"

#include <string.h>

#include "log.h"
#include "tim.h"

/* Private constants ---------------------------------------------------------*/
#define DRV_PWM_TAG "drv_pwm"

/* Private types -------------------------------------------------------------*/

typedef struct {
    TIM_TypeDef* tim;    /**< TIM 外设寄存器基址 */
    uint32_t     channel; /**< HAL 通道号 (TIM_CHANNEL_1 ~ TIM_CHANNEL_4) */
    bool         running;
    bool         initialized;
} drv_pwm_ctx_t;

/* Private variables ---------------------------------------------------------*/

static drv_pwm_ctx_t s_ctx[DRV_PWM_CH_NUM];

/* Private helpers -----------------------------------------------------------*/

static TIM_TypeDef* htim_to_instance(void* htim)
{
    return ((TIM_HandleTypeDef*)htim)->Instance;
}

/**
 * @brief 将 HAL 通道号转换为 CCER CCxE 位掩码
 */
static uint32_t channel_to_ccer_mask(uint32_t channel)
{
    switch (channel) {
    case TIM_CHANNEL_1: return TIM_CCER_CC1E;
    case TIM_CHANNEL_2: return TIM_CCER_CC2E;
    case TIM_CHANNEL_3: return TIM_CCER_CC3E;
    case TIM_CHANNEL_4: return TIM_CCER_CC4E;
    default:            return 0;
    }
}

/* Exported functions --------------------------------------------------------*/

/* --- 初始化 / 生命周期 --- */

drv_pwm_error_t drv_pwm_init(drv_pwm_channel_t ch, void* htim, uint32_t channel)
{
    if (ch >= DRV_PWM_CH_NUM || !htim) {
        return DRV_PWM_ERROR_NULL_PTR;
    }

    drv_pwm_ctx_t* ctx = &s_ctx[ch];

    if (ctx->initialized) {
        drv_pwm_deinit(ch);
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->tim     = htim_to_instance(htim);
    ctx->channel = channel;

    ctx->initialized = true;
    return DRV_PWM_OK;
}

void drv_pwm_deinit(drv_pwm_channel_t ch)
{
    if (ch >= DRV_PWM_CH_NUM) {
        return;
    }

    drv_pwm_ctx_t* ctx = &s_ctx[ch];
    if (!ctx->initialized) {
        return;
    }

    (void)drv_pwm_stop(ch);
    memset(ctx, 0, sizeof(*ctx));
}

bool drv_pwm_is_initialized(drv_pwm_channel_t ch)
{
    if (ch >= DRV_PWM_CH_NUM) {
        return false;
    }
    return s_ctx[ch].initialized;
}

/* --- 占空比控制 --- */

drv_pwm_error_t drv_pwm_set_duty(drv_pwm_channel_t ch, uint32_t duty)
{
    if (ch >= DRV_PWM_CH_NUM) {
        return DRV_PWM_ERROR_INVALID_PARAM;
    }

    drv_pwm_ctx_t* ctx = &s_ctx[ch];
    if (!ctx->initialized) {
        return DRV_PWM_ERROR_UNINITIALIZED;
    }

    /* 直接写 CCR 寄存器（绕过 HAL 宏的类型限制） */
    switch (ctx->channel) {
    case TIM_CHANNEL_1: ctx->tim->CCR1 = duty; break;
    case TIM_CHANNEL_2: ctx->tim->CCR2 = duty; break;
    case TIM_CHANNEL_3: ctx->tim->CCR3 = duty; break;
    case TIM_CHANNEL_4: ctx->tim->CCR4 = duty; break;
    default: return DRV_PWM_ERROR_INVALID_PARAM;
    }

    /* 产生软件更新事件，立即将预装载值传输到影子寄存器 */
    ctx->tim->EGR = TIM_EGR_UG;

    return DRV_PWM_OK;
}

/* --- 运行控制 --- */

drv_pwm_error_t drv_pwm_start(drv_pwm_channel_t ch)
{
    if (ch >= DRV_PWM_CH_NUM) {
        return DRV_PWM_ERROR_INVALID_PARAM;
    }

    drv_pwm_ctx_t* ctx = &s_ctx[ch];
    if (!ctx->initialized) {
        return DRV_PWM_ERROR_UNINITIALIZED;
    }

    if (ctx->running) {
        return DRV_PWM_OK;
    }

    uint32_t ccer_mask = channel_to_ccer_mask(ctx->channel);
    if (ccer_mask == 0) {
        LOG_E(DRV_PWM_TAG, "无效通道 %lu", ctx->channel);
        return DRV_PWM_ERROR_INVALID_PARAM;
    }

    /* 启用 CC 通道输出 */
    ctx->tim->CCER |= ccer_mask;

    /* 启动计数器 */
    ctx->tim->CR1 |= TIM_CR1_CEN;

    ctx->running = true;
    LOG_I(DRV_PWM_TAG, "CH%d 启动, TIM%lu, CCER=0x%04lX",
        (int)ch + 1, (uint32_t)((uintptr_t)ctx->tim), ctx->tim->CCER);

    return DRV_PWM_OK;
}

drv_pwm_error_t drv_pwm_stop(drv_pwm_channel_t ch)
{
    if (ch >= DRV_PWM_CH_NUM) {
        return DRV_PWM_ERROR_INVALID_PARAM;
    }

    drv_pwm_ctx_t* ctx = &s_ctx[ch];
    if (!ctx->initialized) {
        return DRV_PWM_ERROR_UNINITIALIZED;
    }

    if (!ctx->running) {
        return DRV_PWM_OK;
    }

    uint32_t ccer_mask = channel_to_ccer_mask(ctx->channel);

    /* 禁用 CC 通道输出 */
    ctx->tim->CCER &= ~ccer_mask;
    ctx->tim->CR1 &= ~TIM_CR1_CEN;

    ctx->running = false;
    return DRV_PWM_OK;
}
