/**
 * @file    ring_storage_port.h
 * @brief   环形缓冲区 Flash 存储平台抽象接口
 * @author  FOC Development Team
 * @version V1.0.0
 * @date    2026-07-21
 *
 * @note    用户需根据目标 MCU 实现以下接口：
 *          - read:  从 Flash 读取数据
 *          - write: 向 Flash 写入数据（目标区域必须已擦除）
 *          - erase: 擦除 Flash 扇区
 *          - lock/unlock: 保护 Flash 操作的临界区
 *
 *          STM32G4 参考实现见 ring_storage_port_stm32.c（可选）
 */

#ifndef __RING_STORAGE_PORT_H
#define __RING_STORAGE_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stddef.h>
#include <stdint.h>

#include "ring_storage.h"  /* ring_storage_error_t */

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief   从 Flash 读取数据
 * @param[in]  addr      Flash 起始地址
 * @param[out] buf       数据接收缓冲区
 * @param[in]  size      要读取的字节数
 * @return    操作结果错误码
 * @note      实现时应做地址范围检查，防止越界读取
 */
ring_storage_error_t ring_storage_port_read(uint32_t addr, uint8_t* buf, size_t size);

/**
 * @brief   向 Flash 写入数据
 * @param[in]  addr      Flash 起始地址（须对齐到写入颗粒度）
 * @param[in]  buf       要写入的数据缓冲区
 * @param[in]  size      要写入的字节数（须为写入颗粒度的整数倍）
 * @return    操作结果错误码
 * @note      目标区域必须已擦除（全 0xFF）。
 *            STM32G4 双字编程要求 addr 和 size 均 8 字节对齐。
 *            建议实现时跳过全 0xFF 的写入单元以减少 Flash 编程次数。
 */
ring_storage_error_t ring_storage_port_write(uint32_t addr, const uint8_t* buf, size_t size);

/**
 * @brief   擦除 Flash 扇区
 * @param[in]  addr      要擦除的 Flash 起始地址（须对齐到扇区边界）
 * @param[in]  size      要擦除的字节数（须为扇区大小的整数倍）
 * @return    操作结果错误码
 * @note      STM32G4 页大小 2KB，擦除操作不可逆
 */
ring_storage_error_t ring_storage_port_erase(uint32_t addr, size_t size);

/**
 * @brief   进入 Flash 操作临界区
 * @note    实现时应使用 BASEPRI 屏蔽中低优先级中断，允许高优先级中断（如 FOC）继续执行。
 *          支持嵌套调用（引用计数）。
 */
void ring_storage_port_lock(void);

/**
 * @brief   退出 Flash 操作临界区
 * @note    与 lock 配对使用，支持嵌套调用
 */
void ring_storage_port_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* __RING_STORAGE_PORT_H */
