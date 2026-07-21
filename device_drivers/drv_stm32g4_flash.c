// Created by maximillian on 2026-07-06.
//

/**
 * @file    drv_stm32g4_flash.c
 * @author  maximillian
 * @version V1.2.leite
 * @version V1.2.0
 * @date    2026-07-06
 * @brief   EasyFlash Flash操作接口 - STM32G474RBTx 移植层
 *
 * Copyright (c) 2026  All rights reserved.
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "drv_stm32g4_flash.h"
#include "log.h"

#include "stm32g4xx_hal.h"

#include <string.h>

/* Private constants ---------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

static uint64_t s_write_buf; /* Flash 写入临时缓冲区 (64-bit 对齐) */
static uint64_t s_read_buf; /* Flash 读取校验缓冲区 (64-bit 对齐) */
static uint32_t s_env_lock_depth = 0; /* 嵌套锁深度 */

static uint32_t s_cached_page_size = 0; /* 缓存的页大小，ef_port_init() 中初始化 */
static uint32_t s_cached_bank_size = 0; /* 缓存的 Bank 大小，ef_port_init() 中初始化 */

/* Private const -------------------------------------------------------------*/

static const uint32_t FLASH_BASE_ADDR = 0x08000000U; /* Flash 基地址 */
static const uint32_t FLASH_PROGRAM_SIZE = 8; /* 编程粒度: 64-bit 双字 */
static const uint64_t FLASH_ERASED_VAL = (~0ULL); /* Flash 擦除后默认值 */

/* Private function prototypes -----------------------------------------------*/

static void ef_port_env_lock(void);
static void ef_port_env_unlock(void);

/**
 * @brief  获取当前硬件配置下的单页大小 (4KB 或 2KB)
 */
static uint32_t GetPageSize(void)
{
    return s_cached_page_size;
}

/**
 * @brief  获取单个 Bank 的物理大小 (对于G474，单/双Bank对应的逻辑切分线不同)
 */
static uint32_t GetBankSize(void)
{
    return s_cached_bank_size;
}

/**
 * @brief  根据输入物理地址计算其所在的物理 Bank
 */
static uint32_t GetBankNumber(uint32_t addr)
{
    if (GetPageSize() == 0x1000U) {
        /* 单 Bank 模式: 只存在 Bank 1 */
        return FLASH_BANK_1;
    }

    /* 双 Bank 模式: 根据地址判断所属 Bank */
    if (addr < (FLASH_BASE_ADDR + GetBankSize())) {
        return FLASH_BANK_1;
    }
    return FLASH_BANK_2;
}

/* Exported functions --------------------------------------------------------*/

uint32_t ef_port_get_page_size(void)
{
    return s_cached_page_size;
}

void ef_port_init(void)
{
    if (s_cached_page_size != 0U) {
        return;
    }

    if (READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK) == 0U) {
        /* 单 Bank 模式: 4KB 页, 总 Flash 128KB */
        s_cached_page_size = 0x1000U;
        s_cached_bank_size = 0x20000U; /* 128 KB, 单个 Bank=整个 Flash */
        LOG_I("flash", "Init: 单 Bank 模式, 页=%luKB, Bank=%luKB",
            (unsigned long)(s_cached_page_size >> 10),
            (unsigned long)(s_cached_bank_size >> 10));
    } else {
        /* 双 Bank 模式: 2KB 页, 每 Bank 64KB */
        s_cached_page_size = 0x800U;
        s_cached_bank_size = 0x10000U; /* 64 KB, 单个 Bank */
        LOG_I("flash", "Init: 双 Bank 模式, 页=%luKB, Bank=%luKB",
            (unsigned long)(s_cached_page_size >> 10),
            (unsigned long)(s_cached_bank_size >> 10));
    }
}

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
    uint32_t page_size = GetPageSize();
    uint32_t bank_size = GetBankSize();

    EF_ASSERT(addr % page_size == 0);
    EF_ASSERT(size % page_size == 0);

    LOG_I("flash", "Erase: addr=0x%08lX, size=%lu, page_size=%lu",
        (unsigned long)addr, (unsigned long)size,
        (unsigned long)page_size);

    ef_port_env_lock();
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    /*
     * 在双 Bank 模式下，擦除区域可能跨越 Bank 边界（例如 APP_A 40KB
     * 横跨 Bank1 尾部和 Bank2 头部）。此时需要分 Bank 多次擦除，
     * 每次只擦除一个 Bank 范围内的连续页。
     */
    uint32_t current_addr = addr;
    size_t remaining = size;

    while (remaining > 0) {
        uint32_t bank = GetBankNumber(current_addr);
        uint32_t bank_base = (bank == FLASH_BANK_1)
            ? FLASH_BASE_ADDR
            : (FLASH_BASE_ADDR + bank_size);
        uint32_t bank_end = bank_base + bank_size;

        /* 当前 Bank 内可擦除的最大字节数 */
        size_t chunk = bank_end - current_addr;
        if (chunk > remaining) {
            chunk = remaining;
        }
        uint32_t chunk_pages = (uint32_t)(chunk / page_size);

        uint32_t page = (current_addr - bank_base) / page_size;

        LOG_I("flash", "Erase chunk: bank=%lu, page=%lu, nb=%lu",
            (unsigned long)bank, (unsigned long)page,
            (unsigned long)chunk_pages);

        FLASH_EraseInitTypeDef erase_init = {
            .TypeErase = FLASH_TYPEERASE_PAGES,
            .Banks = bank,
            .Page = page,
            .NbPages = chunk_pages,
        };

        if (HAL_FLASHEx_Erase(&erase_init, &error) != HAL_OK) {
            LOG_E("flash", "Erase err: addr=0x%08lX bank=%lu page=%lu HAL_Err=0x%08lX",
                (unsigned long)current_addr, (unsigned long)bank,
                (unsigned long)page, (unsigned long)HAL_FLASH_GetError());
            result = EF_ERASE_ERR;
            goto exit_erase;
        }

        current_addr += chunk;
        remaining -= chunk;
    }

exit_erase:
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();

    __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
    __HAL_FLASH_INSTRUCTION_CACHE_RESET();
    __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();

    HAL_FLASH_Lock();
    ef_port_env_unlock();
    return result;
}

void ef_port_cache_invalidate(void)
{
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
}

ef_err_code_t ef_port_write(uint32_t addr, const uint32_t* buf, size_t size)
{
    ef_err_code_t result = EF_NO_ERR;
    const uint8_t* src = (const uint8_t*)buf;

    LOG_I("flash", "Write: addr=0x%08lX, size=%lu",
        (unsigned long)addr, (unsigned long)size);

    ef_port_env_lock();

    HAL_FLASH_Unlock();

    /* 清除所有旧错误标志 */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    for (size_t i = 0; i < size; i += FLASH_PROGRAM_SIZE) {
        size_t copy_len = (size - i >= FLASH_PROGRAM_SIZE) ? FLASH_PROGRAM_SIZE : (size - i);

        if (copy_len < FLASH_PROGRAM_SIZE) {
            s_write_buf = FLASH_ERASED_VAL;
            memcpy(&s_write_buf, src, copy_len);
        } else {
            memcpy(&s_write_buf, src, FLASH_PROGRAM_SIZE);
        }

        /* 只擦除值（全0xFF）才需要跳过编程 */
        if (s_write_buf != FLASH_ERASED_VAL) {
            /* 使用HAL库进行双字编程，确保正确的时序和错误处理 */
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, s_write_buf) != HAL_OK) {
                uint32_t error = HAL_FLASH_GetError();
                LOG_E("flash", "Flash 编程错误: i=%u, addr=0x%08lX, HAL_Error=0x%08lX",
                    (unsigned)i, addr, (unsigned long)error);
                result = EF_WRITE_ERR;
                goto exit_write;
            }
        }

        /* 读回验证 */
        s_read_buf = *(volatile uint64_t*)addr;
        /* 直接与对齐过的 s_write_buf 进行比对，避免在未对齐的 src 上执行 64-bit 指针转换 */
        if (s_read_buf != s_write_buf) {
            LOG_E("flash", "Flash 读回不匹配: i=%u, addr=0x%08lX, "
                           "written=0x%016llX, readback=0x%016llX",
                (unsigned)i, addr,
                (unsigned long long)s_write_buf,
                (unsigned long long)s_read_buf);
            result = EF_WRITE_ERR;
            goto exit_write;
        }

        addr += FLASH_PROGRAM_SIZE;
        src += copy_len;
    }

exit_write:
    /* 写入后清空并重置 ART Accelerator 缓存，防止 CPU 读到旧数据
     * 注意：HAL 要求 cache reset 必须在 cache 禁用状态下执行 */
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();

    __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
    __HAL_FLASH_INSTRUCTION_CACHE_RESET();
    __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();

    HAL_FLASH_Lock();

    ef_port_env_unlock();
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