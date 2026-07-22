/**
 * @file    drv_stm32f4_flash.h
 * @brief   STM32F407 Flash 底层驱动 — 适配 hal_flash 抽象层
 *
 * 导出 f4_ops (VTable) 和 f4_sectors (扇区表) 供 HAL 层引用。
 * 仅在定义了 HAL_FLASH_CHIP_STM32F4 时内容可见。
 */

#ifndef __DRV_STM32F4_FLASH_H
#define __DRV_STM32F4_FLASH_H

#include "hal_flash_base.h"

#ifdef HAL_FLASH_CHIP_STM32F4

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 扇区描述类型 (内部使用) ============================================*/

typedef struct {
    uint32_t base;
    uint32_t size;
} f4_sector_desc_t;

/* ====== 导出符号 ===========================================================*/

extern const f4_sector_desc_t f4_sectors[];
extern hal_flash_dev_t f4_dev;

#ifdef __cplusplus
}
#endif

#endif /* HAL_FLASH_CHIP_STM32F4 */

#endif /* __DRV_STM32F4_FLASH_H */
