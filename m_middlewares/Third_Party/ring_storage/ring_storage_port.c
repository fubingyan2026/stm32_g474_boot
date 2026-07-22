/**
 * @file    ring_storage_port.c
 * @brief   Ring Storage 平台移植层 - STM32G474RBTx
 * @author  maximilian
 * @version V1.0.0
 * @date    2026-07-21
 *
 * @note    实现 ring_storage_port.h 声明的 5 个平台接口，
 *          基于 hal_flash 抽象层 API。
 *
 *          锁策略：
 *          ring_storage_port_lock 直接委托 hal_flash_lock（嵌套中断锁），
 *          hal_flash 内部使用引用计数，支持嵌套调用。
 */

/* Includes ------------------------------------------------------------------*/
#include "ring_storage.h"
#include "ring_storage_port.h"

#include "hal_flash.h"
#include "log.h"

#include <string.h>

/* Private constants ---------------------------------------------------------*/

/** @brief 本文件日志开关：置 0 屏蔽本文件全部打印 */
#define RING_STORAGE_PORT_LOG_ENABLE 0

#if RING_STORAGE_PORT_LOG_ENABLE
#define RING_STORAGE_PORT_LOG_E(...) LOG_E("rs_port", __VA_ARGS__)
#define RING_STORAGE_PORT_LOG_W(...) LOG_W("rs_port", __VA_ARGS__)
#define RING_STORAGE_PORT_LOG_I(...) LOG_I("rs_port", __VA_ARGS__)
#define RING_STORAGE_PORT_LOG_D(...) LOG_D("rs_port", __VA_ARGS__)
#else
#define RING_STORAGE_PORT_LOG_E(...) ((void)0)
#define RING_STORAGE_PORT_LOG_W(...) ((void)0)
#define RING_STORAGE_PORT_LOG_I(...) ((void)0)
#define RING_STORAGE_PORT_LOG_D(...) ((void)0)
#endif

/* Exported functions --------------------------------------------------------*/

ring_storage_error_t ring_storage_port_read(uint32_t addr,
                                            uint8_t* buf,
                                            size_t size)
{
    hal_flash_err_t ret = hal_flash_read(addr - hal_flash_get_caps()->addr, buf, size);

    if (ret != HAL_FLASH_OK) {
        RING_STORAGE_PORT_LOG_E( "Flash 读取失败: %s (addr=0x%08lX, size=%lu, err=%d)",
              hal_flash_err_str(ret),
              (unsigned long)addr, (unsigned long)size, ret);
        return RING_STORAGE_ERROR_FLASH_READ;
    }

    return RING_STORAGE_OK;
}

ring_storage_error_t ring_storage_port_write(uint32_t addr,
                                             const uint8_t* buf,
                                             size_t size)
{
    hal_flash_err_t ret = hal_flash_write(addr - hal_flash_get_caps()->addr,
                                         buf,
                                         size);

    if (ret != HAL_FLASH_OK) {
        RING_STORAGE_PORT_LOG_E( "Flash 写入失败: %s (addr=0x%08lX, size=%lu, err=%d)",
              hal_flash_err_str(ret),
              (unsigned long)addr, (unsigned long)size, ret);
        return RING_STORAGE_ERROR_FLASH_WRITE;
    }

    return RING_STORAGE_OK;
}

ring_storage_error_t ring_storage_port_erase(uint32_t addr, size_t size)
{
    hal_flash_err_t ret = hal_flash_erase(addr - hal_flash_get_caps()->addr, size);

    if (ret != HAL_FLASH_OK) {
        RING_STORAGE_PORT_LOG_E( "Flash 擦除失败: %s (addr=0x%08lX, size=%lu, err=%d)",
              hal_flash_err_str(ret),
              (unsigned long)addr, (unsigned long)size, ret);
        return RING_STORAGE_ERROR_FLASH_ERASE;
    }

    return RING_STORAGE_OK;
}

void ring_storage_port_lock(void)
{
    hal_flash_lock();
}

void ring_storage_port_unlock(void)
{
    hal_flash_unlock();
}
