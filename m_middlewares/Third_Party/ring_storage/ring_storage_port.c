/**
 * @file    ring_storage_port.c
 * @brief   Ring Storage 平台移植层 - STM32G474RBTx
 * @author  FOC Development Team
 * @version V1.0.0
 * @date    2026-07-21
 *
 * @note    实现 ring_storage_port.h 声明的 5 个平台接口，
 *          基于 drv_stm32g4_flash.c 的 ef_port_* API。
 *
 *          锁嵌套说明：
 *          ring_storage_port_lock 使用 BASEPRI（允许高优先级中断继续执行），
 *          而 ef_port_env_lock 使用 __disable_irq（屏蔽所有中断）。
 *          嵌套时：BASEPRI 屏障 → __disable_irq → __enable_irq 解除的是
 *          PRIMASK，但 BASEPRI 仍生效，保证临界区完整。
 */

/* Includes ------------------------------------------------------------------*/
#include "ring_storage.h"
#include "ring_storage_port.h"

#include "drv_stm32g4_flash.h"
#include "log.h"

#include "stm32g4xx_hal.h"         /* __get_BASEPRI, __set_BASEPRI */

#include <string.h>

/* Private constants ---------------------------------------------------------*/

/** @brief 本文件日志开关：置 0 屏蔽本文件全部打印 */
#define RING_STORAGE_PORT_LOG_ENABLE 1

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

/* Private variables ---------------------------------------------------------*/

static uint32_t s_lock_depth = 0;          /**< 嵌套锁深度 */
static uint32_t s_basepri_backup = 0;      /**< 保存的 BASEPRI 值 */

/* Exported functions --------------------------------------------------------*/

ring_storage_error_t ring_storage_port_read(uint32_t addr,
                                            uint8_t* buf,
                                            size_t size)
{
    ef_err_code_t ret = ef_port_read(addr, (uint32_t*)(void*)buf, size);

    if (ret != EF_NO_ERR) {
        RING_STORAGE_PORT_LOG_E( "Flash 读取失败: addr=0x%08lX, size=%lu, err=%d",
              (unsigned long)addr, (unsigned long)size, ret);
        return RING_STORAGE_ERROR_FLASH_READ;
    }

    return RING_STORAGE_OK;
}

ring_storage_error_t ring_storage_port_write(uint32_t addr,
                                             const uint8_t* buf,
                                             size_t size)
{
    ef_err_code_t ret = ef_port_write(addr,
                                      (const uint32_t*)(const void*)buf,
                                      size);

    if (ret != EF_NO_ERR) {
        RING_STORAGE_PORT_LOG_E( "Flash 写入失败: addr=0x%08lX, size=%lu, err=%d",
              (unsigned long)addr, (unsigned long)size, ret);
        return RING_STORAGE_ERROR_FLASH_WRITE;
    }

    return RING_STORAGE_OK;
}

ring_storage_error_t ring_storage_port_erase(uint32_t addr, size_t size)
{
    ef_err_code_t ret = ef_port_erase(addr, size);

    if (ret != EF_NO_ERR) {
        RING_STORAGE_PORT_LOG_E( "Flash 擦除失败: addr=0x%08lX, size=%lu, err=%d",
              (unsigned long)addr, (unsigned long)size, ret);
        return RING_STORAGE_ERROR_FLASH_ERASE;
    }

    return RING_STORAGE_OK;
}

void ring_storage_port_lock(void)
{
    if (s_lock_depth == 0) {
        /* 保存当前 BASEPRI，设置为 0x50：
         *  - 屏蔽优先级 ≤ 0x50 的中断（含所有常规外设中断）
         *  - 允许 FAULT 及更高优先级中断继续执行（如 FOC 控制环） */
        s_basepri_backup = __get_BASEPRI();
        __set_BASEPRI(0x50);
    }
    s_lock_depth++;
}

void ring_storage_port_unlock(void)
{
    if (s_lock_depth > 0) {
        s_lock_depth--;
    }
    if (s_lock_depth == 0) {
        __set_BASEPRI(s_basepri_backup);
    }
}
