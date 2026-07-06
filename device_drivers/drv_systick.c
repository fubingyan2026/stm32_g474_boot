//
// Created by fubingyan on 25-6-7.
//

/**
 * @file    hal_systick.c
 * @author  fubingyan
 * @version V1.0.0
 * @date    2025-06-07
 * @brief   SysTick 定时工具（延时、微秒/毫秒时间戳、性能计时）
 */

#include "drv_systick.h"

#include "main.h"

/* Private variables ---------------------------------------------------------*/

static uint8_t fac_us = 0;
static uint32_t fac_ms = 0;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化延时参数
 *
 * 根据系统核心时钟计算出微秒和毫秒的系数。
 */
void delay_init(void)
{
    fac_us = SystemCoreClock / 1000000;
    fac_ms = SystemCoreClock / 1000;
}

/**
 * @brief 微秒级延时
 *
 * 通过 SysTick 定时器实现微秒级别的延时。
 *
 * @param nus 需要延时的微秒数
 */
void delay_us(const uint16_t nus)
{
    uint32_t ticks = 0;
    uint32_t told = 0;
    uint32_t tnow = 0;
    uint32_t tcnt = 0;
    uint32_t reload = 0;
    reload = SysTick->LOAD;
    ticks = nus * fac_us;
    told = SysTick->VAL;
    for (;;) {
        tnow = SysTick->VAL;
        if (tnow != told) {
            if (tnow < told) {
                tcnt += told - tnow;
            } else {
                tcnt += reload - tnow + told;
            }
            told = tnow;
            if (tcnt >= ticks) {
                break;
            }
        }
    }
}

/**
 * @brief 毫秒级延时
 *
 * 使用 SysTick 定时器实现精确的毫秒级延时。
 *
 * @param nms 需要延时的毫秒数
 */
void delay_ms(const uint16_t nms)
{
    HAL_Delay(nms);
}

/**
 * @brief 获取微秒级时间戳
 *
 * 结合 HAL_GetTick() 和 SysTick 当前计数值计算出精确的微秒时间。
 *
 * @return 自系统启动以来的微秒数
 */
uint32_t micros(void)
{
    // 读取 CTRL 寄存器清除 COUNTFLAG 标志位
    SysTick->CTRL;
    uint32_t m = HAL_GetTick();
    const uint32_t tms = SysTick->LOAD + 1;
    __IO uint32_t u = tms - SysTick->VAL;
    if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == SysTick_CTRL_COUNTFLAG_Msk) {
        m = HAL_GetTick();
        u = tms - SysTick->VAL;
    }
    return m * 1000 + u * 1000 / tms;
}

/**
 * @brief 获取系统运行时间（毫秒）
 * @return 自系统启动以来的毫秒数
 */
uint32_t millis(void)
{
    return HAL_GetTick();
}
