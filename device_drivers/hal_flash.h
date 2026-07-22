/**
 * @file    hal_flash.h
 * @brief   Flash 硬件抽象层 — 单例模式，编译时选型
 *
 * 嵌入式场景下 Flash 只有当前芯片那一块，因此采用单例模式：
 *   - 编译时通过 HAL_FLASH_CHIP_xxx 宏选择目标芯片驱动
 *   - 运行时通过 hal_flash_dev() 获取唯一设备实例
 *   - 上层 API 不需要传 dev 参数，更简洁
 *
 * 层次关系:
 *   上层 (EasyFlash/OTA/文件系统)
 *     ↓ 调用 hal_flash_read/write/erase (无 dev 参数)
 *   本层 (hal_flash)  — 单例管理 + 锁管理 + 参数校验
 *     ↓ 通过唯一实例的内嵌 ops 调用
 *   底层 (drv_stm32f4_flash / drv_stm32g4_flash / ...)
 *     ↓ 调用 HAL 库
 *   硬件
 *
 * 依赖方向:
 *   hal_flash.h  →  hal_flash_base.h  ←  drv_stm32xx_flash.h
 *   (上层 API)       (基础契约)           (底层驱动)
 *
 * 选型宏 (在编译选项或 hal_flash_conf.h 中定义):
 *   HAL_FLASH_CHIP_STM32F4  — 选择 STM32F4 驱动
 *   HAL_FLASH_CHIP_STM32G4  — 选择 STM32G4 驱动
 *   HAL_FLASH_CHIP_STM32H7  — 选择 STM32H7 驱动 (预留)
 *
 * 锁策略:
 *   默认使用裸机嵌套中断锁 (__disable_irq/__enable_irq)；
 *   RTOS 环境下可通过 hal_flash_set_lock_cb() 替换为 mutex。
 */

#ifndef __HAL_FLASH_H
#define __HAL_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 类型定义 (从 hal_flash_base.h 引入) ===============================*/
#include "hal_flash_base.h"

/* ====== 芯片选型 ===========================================================*/

/*
 * 通过宏选择编译哪个芯片驱动。
 * 可在编译选项 (-DHAL_FLASH_CHIP_STM32F4) 中定义，
 * 也可在下方手动取消注释。
 */
#if !defined(HAL_FLASH_CHIP_STM32F4) && !defined(HAL_FLASH_CHIP_STM32G4) && !defined(HAL_FLASH_CHIP_STM32H7)
/* 默认选择 F4，可根据项目修改 */
#define HAL_FLASH_CHIP_STM32G4
#endif

/* ====== 公共 API (无需传 dev) =============================================*/

hal_flash_err_t hal_flash_init(void);
hal_flash_err_t hal_flash_read(uint32_t offset, uint8_t *buf, size_t size);
hal_flash_err_t hal_flash_write(uint32_t offset, const uint8_t *buf, size_t size);
hal_flash_err_t hal_flash_erase(uint32_t offset, size_t size);
void            hal_flash_cache_invalidate(void);
uint32_t        hal_flash_erase_size_at(uint32_t offset);
hal_flash_err_t hal_flash_write_protect(uint32_t offset, size_t size, bool enable);
hal_flash_err_t hal_flash_crc_verify(uint32_t offset, size_t size, uint32_t *crc_out);
hal_flash_err_t hal_flash_otp_read(uint32_t offset, uint8_t *buf, size_t size);
hal_flash_err_t hal_flash_otp_write(uint32_t offset, const uint8_t *buf, size_t size);
hal_flash_err_t hal_flash_erase_async(uint32_t offset, size_t size);

/* ====== 辅助 API ==========================================================*/

hal_flash_dev_t*        hal_flash_dev(void);
void                    hal_flash_lock(void);
void                    hal_flash_unlock(void);
void                    hal_flash_set_lock_cb(hal_flash_lock_cb lock, hal_flash_lock_cb unlock);
void                    hal_flash_set_event_cb(hal_flash_event_cb cb, void *arg);
const hal_flash_caps_t* hal_flash_get_caps(void);
const char*             hal_flash_err_str(hal_flash_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_FLASH_H */
