//
// Created by maximillian on 2026-07-06.
//

/**
 * @file    drv_stm32g4_flash.c
 * @author  maximillian
 * @version V1.2.0
 * @date    2026-07-6
 * @brief   EasyFlash Flash操作接口 - STM32G474RBTx 移植层
 * @attention
 *
 * STM32G474RBTx: 128KB Flash, 2KB/页, 64-bit 双字编程。
 * 使用页擦除和双字编程，支持读写校验。
 * 擦除/写入操作前解锁 Flash，操作完成后上锁。
 *
 * Copyright (c) 2026  All rights reserved.
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "drv_stm32g4_flash.h"

#include "stm32g4xx_hal.h"

#include <string.h>

/* Private constants ---------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

static uint64_t s_write_buf; /* Flash 写入临时缓冲区 (64-bit 对齐) */
static uint64_t s_read_buf; /* Flash 读取校验缓冲区 (64-bit 对齐) */
static uint32_t s_env_lock_depth = 0; /* 嵌套锁深度 */

/* Private const -------------------------------------------------------------*/

static const uint32_t FLASH_BASE_ADDR = 0x08000000U; /* Flash 基地址 */
static const uint32_t PAGE_SIZE = 0x800U; /* Flash 页大小: 2KB */
static const uint32_t EF_ERASE_MIN_SIZE = PAGE_SIZE; /* 最小擦除单位: 1 页 */
static const uint32_t FLASH_PROGRAM_SIZE = 8; /* 编程粒度: 64-bit 双字 */
static const uint64_t FLASH_ERASED_VAL = (~0ULL); /* Flash 擦除后默认值 */

/* Private function prototypes -----------------------------------------------*/
void ef_port_env_lock(void);
void ef_port_env_unlock(void);

/* Exported functions --------------------------------------------------------*/

ef_err_code_t ef_port_read(uint32_t addr, uint32_t* buf, size_t size)
{
    uint8_t* dst = (uint8_t*)buf;

    for (size_t i = 0; i < size; i++, addr++, dst++) {
        *dst = *(volatile uint8_t*)addr;
    }

    return EF_NO_ERR;
}

ef_err_code_t ef_port_erase(uint32_t addr, size_t size)
{
    ef_err_code_t result = EF_NO_ERR;
    uint32_t error = 0;

    EF_ASSERT(addr % EF_ERASE_MIN_SIZE == 0);

    size_t erase_pages = size / PAGE_SIZE;
    if (size % PAGE_SIZE != 0) {
        erase_pages++;
    }

    FLASH_EraseInitTypeDef erase_init = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Page = (addr - FLASH_BASE_ADDR) / PAGE_SIZE,
        .NbPages = 1,
        .Banks = FLASH_BANK_1,
    };

    HAL_FLASH_Unlock();

    __HAL_FLASH_CLEAR_FLAG(
        FLASH_FLAG_EOP
        | FLASH_FLAG_WRPERR
        | FLASH_FLAG_OPTVERR
        | FLASH_FLAG_PROGERR
        | FLASH_FLAG_PGSERR
        | FLASH_FLAG_PGAERR);

    for (size_t i = 0; i < erase_pages; i++) {
        if (HAL_FLASHEx_Erase(&erase_init, &error) != HAL_OK) {
            result = EF_ERASE_ERR;
            goto exit_erase;
        }
        erase_init.Page++;
    }

exit_erase:
    HAL_FLASH_Lock();
    return result;
}

ef_err_code_t ef_port_write(uint32_t addr, const uint32_t* buf, size_t size)
{
    ef_err_code_t result = EF_NO_ERR;
    HAL_StatusTypeDef status = HAL_OK;
    const uint8_t* src = (const uint8_t*)buf;

    HAL_FLASH_Unlock();

    __HAL_FLASH_CLEAR_FLAG(
        FLASH_FLAG_EOP
        | FLASH_FLAG_OPERR
        | FLASH_FLAG_WRPERR
        | FLASH_FLAG_PGAERR
        | FLASH_FLAG_PGSERR
        | FLASH_FLAG_PROGERR);

    for (size_t i = 0; i < size; i += FLASH_PROGRAM_SIZE) {
        memcpy(&s_write_buf, src, FLASH_PROGRAM_SIZE);

        if (s_write_buf != FLASH_ERASED_VAL) {
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                addr, s_write_buf);
        }

        s_read_buf = *(volatile uint64_t*)addr;

        if ((s_read_buf != *(const volatile uint64_t*)src)
            || (status != HAL_OK)) {
            result = EF_WRITE_ERR;
            goto exit_write;
        }

        addr += FLASH_PROGRAM_SIZE;
        src += FLASH_PROGRAM_SIZE;
    }

exit_write:
    HAL_FLASH_Lock();
    return result;
}

void ef_port_env_lock(void)
{
    if (s_env_lock_depth == 0) {
        __disable_irq();
    }
    s_env_lock_depth++;
}

void ef_port_env_unlock(void)
{
    if (s_env_lock_depth > 0) {
        s_env_lock_depth--;
    }
    if (s_env_lock_depth == 0) {
        __enable_irq();
    }
}

/* Private functions ---------------------------------------------------------*/
