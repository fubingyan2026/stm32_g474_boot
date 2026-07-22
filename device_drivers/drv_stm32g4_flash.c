/**
 * @file    drv_stm32g4_flash.c
 * @brief   STM32G474 Flash 底层驱动 — 适配 hal_flash 抽象层
 *
 * 实现 hal_flash_ops_t 接口，导出 g4_ops 和 g4_priv 供 HAL 层引用。
 * 支持单/双 Bank 自动检测，64-bit 双字编程 + 读回校验。
 * 仅在定义了 HAL_FLASH_CHIP_STM32G4 时编译，否则本文件为空。
 */

#include "hal_flash.h"

#ifdef HAL_FLASH_CHIP_STM32G4

/* Includes ------------------------------------------------------------------*/
#include "drv_stm32g4_flash.h"
#include "log.h"

#include "stm32g4xx_hal.h"

#include <string.h>

/* 模块日志开关 ----------------------------------------------------------------*/

#define FLASH_LOG_ENABLE 0

#if FLASH_LOG_ENABLE
#define FLASH_LOG_E(...) LOG_E("flash", __VA_ARGS__)
#define FLASH_LOG_W(...) LOG_W("flash", __VA_ARGS__)
#define FLASH_LOG_I(...) LOG_I("flash", __VA_ARGS__)
#define FLASH_LOG_D(...) LOG_D("flash", __VA_ARGS__)
#else
#define FLASH_LOG_E(...) ((void)0)
#define FLASH_LOG_W(...) ((void)0)
#define FLASH_LOG_I(...) ((void)0)
#define FLASH_LOG_D(...) ((void)0)
#endif

/* Private constants ---------------------------------------------------------*/

static const uint32_t FLASH_PROGRAM_SIZE = 8;
static const uint64_t FLASH_ERASED_VAL = (~0ULL);
static const uint32_t FLASH_BASE_ADDR = 0x08000000U;
static const uint32_t FLASH_TOTAL_SIZE = 0x00020000U; /* 128 KB */

/* Private types -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

static uint64_t s_write_buf;
static uint64_t s_read_buf;

/* G4 私有数据实例 (HAL 层 s_dev.priv 引用) */
g4_priv_data_t g4_priv = {
    .page_size = 0, /* init 中动态检测 */
    .bank_size = 0,
};

/* Private function prototypes -----------------------------------------------*/

static uint32_t get_page_size(const g4_priv_data_t* priv)
{
    return priv->page_size;
}

static uint32_t get_bank_size(const g4_priv_data_t* priv)
{
    return priv->bank_size;
}

static uint32_t get_bank_number(uint32_t addr)
{
    if (get_page_size(&g4_priv) == 0x1000U) {
        return FLASH_BANK_1;
    }
    if (addr < (FLASH_BASE_ADDR + get_bank_size(&g4_priv))) {
        return FLASH_BANK_1;
    }
    return FLASH_BANK_2;
}

/* ====== hal_flash_ops_t 接口实现 ========================================== */

static hal_flash_err_t g4_init(void)
{
    if (g4_priv.page_size != 0U) {
        return HAL_FLASH_OK;
    }

    hal_flash_dev_t* dev = hal_flash_dev();

    if (READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK) == 0U) {
        g4_priv.page_size = 0x1000U;
        g4_priv.bank_size = 0x20000U;
        FLASH_LOG_I("初始化: 单 Bank 模式, 页=%luKB, Bank=%luKB",
            (unsigned long)(g4_priv.page_size >> 10),
            (unsigned long)(g4_priv.bank_size >> 10));
    } else {
        g4_priv.page_size = 0x800U;
        g4_priv.bank_size = 0x10000U;
        FLASH_LOG_I("初始化: 双 Bank 模式, 页=%luKB, Bank=%luKB",
            (unsigned long)(g4_priv.page_size >> 10),
            (unsigned long)(g4_priv.bank_size >> 10));
    }

    /* 初始化后更新 caps 中的 erase_size */
    dev->caps.erase_size = g4_priv.page_size;

    return HAL_FLASH_OK;
}

static hal_flash_err_t g4_read(uint32_t offset, uint8_t* buf, size_t size)
{
    uint32_t addr = FLASH_BASE_ADDR + offset;
    uint8_t* dst = buf;

    for (size_t i = 0; i < size; i++, addr++, dst++) {
        *dst = *(volatile uint8_t*)addr;
    }

    return HAL_FLASH_OK;
}

static hal_flash_err_t g4_erase(uint32_t offset, size_t size)
{
    hal_flash_err_t result = HAL_FLASH_OK;
    uint32_t error = 0;
    uint32_t page_size = get_page_size(&g4_priv);
    uint32_t bank_size = get_bank_size(&g4_priv);

    uint32_t addr = FLASH_BASE_ADDR + offset;

    if (addr % page_size != 0) {
        FLASH_LOG_E("擦除: 地址未页对齐, addr=0x%08lX, page_size=%lu",
            (unsigned long)addr, (unsigned long)page_size);
        return HAL_FLASH_ALIGN_ERR;
    }
    if (size % page_size != 0) {
        FLASH_LOG_E("擦除: 大小未页对齐, size=%lu, page_size=%lu",
            (unsigned long)size, (unsigned long)page_size);
        return HAL_FLASH_ALIGN_ERR;
    }

    FLASH_LOG_I("擦除: addr=0x%08lX, size=%lu, page_size=%lu",
        (unsigned long)addr, (unsigned long)size,
        (unsigned long)page_size);

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    uint32_t current_addr = addr;
    size_t remaining = size;

    while (remaining > 0) {
        uint32_t bank = get_bank_number(current_addr);
        uint32_t bank_base = (bank == FLASH_BANK_1)
            ? FLASH_BASE_ADDR
            : (FLASH_BASE_ADDR + bank_size);
        uint32_t bank_end = bank_base + bank_size;

        size_t chunk = bank_end - current_addr;
        if (chunk > remaining) {
            chunk = remaining;
        }
        uint32_t chunk_pages = (uint32_t)(chunk / page_size);
        uint32_t page = (current_addr - bank_base) / page_size;

        FLASH_LOG_I("擦除分片: bank=%lu, page=%lu, nb=%lu",
            (unsigned long)bank, (unsigned long)page,
            (unsigned long)chunk_pages);

        FLASH_EraseInitTypeDef erase_init = {
            .TypeErase = FLASH_TYPEERASE_PAGES,
            .Banks = bank,
            .Page = page,
            .NbPages = chunk_pages,
        };

        if (HAL_FLASHEx_Erase(&erase_init, &error) != HAL_OK) {
            FLASH_LOG_E("擦除错误: addr=0x%08lX, bank=%lu, page=%lu, HAL_Err=0x%08lX",
                (unsigned long)current_addr, (unsigned long)bank,
                (unsigned long)page, (unsigned long)HAL_FLASH_GetError());
            result = HAL_FLASH_ERASE_ERR;
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
    return result;
}

static hal_flash_err_t g4_write(uint32_t offset, const uint8_t* buf, size_t size)
{
    hal_flash_err_t result = HAL_FLASH_OK;
    uint32_t addr = FLASH_BASE_ADDR + offset;
    const uint8_t* src = buf;

    FLASH_LOG_I("写入: addr=0x%08lX, size=%lu",
        (unsigned long)addr, (unsigned long)size);

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    for (size_t i = 0; i < size; i += FLASH_PROGRAM_SIZE) {
        size_t copy_len = (size - i >= FLASH_PROGRAM_SIZE) ? FLASH_PROGRAM_SIZE : (size - i);

        if (copy_len < FLASH_PROGRAM_SIZE) {
            s_write_buf = FLASH_ERASED_VAL;
            memcpy(&s_write_buf, src, copy_len);
        } else {
            memcpy(&s_write_buf, src, FLASH_PROGRAM_SIZE);
        }

        if (s_write_buf != FLASH_ERASED_VAL) {
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, s_write_buf) != HAL_OK) {
                uint32_t error = HAL_FLASH_GetError();
                FLASH_LOG_E("Flash 编程错误: i=%u, addr=0x%08lX, HAL_Error=0x%08lX",
                    (unsigned)i, addr, (unsigned long)error);
                result = HAL_FLASH_WRITE_ERR;
                goto exit_write;
            }
        }

        s_read_buf = *(volatile uint64_t*)addr;
        if (s_read_buf != s_write_buf) {
            FLASH_LOG_E("Flash 读回不匹配: i=%u, addr=0x%08lX, "
                        "written=0x%016llX, readback=0x%016llX",
                (unsigned)i, addr,
                (unsigned long long)s_write_buf,
                (unsigned long long)s_read_buf);
            result = HAL_FLASH_WRITE_ERR;
            goto exit_write;
        }

        addr += FLASH_PROGRAM_SIZE;
        src += copy_len;
    }

exit_write:
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();

    __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
    __HAL_FLASH_INSTRUCTION_CACHE_RESET();
    __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();

    HAL_FLASH_Lock();
    return result;
}

static void g4_cache_invalidate(void)
{
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
}

/* ====== 设备实例导出 ======================================================== */

hal_flash_dev_t g4_dev = {
    .name = "stm32g4",
    .ops = {
        .init = g4_init,
        .read = g4_read,
        .write = g4_write,
        .erase = g4_erase,
        .cache_invalidate = g4_cache_invalidate,
    },
    .caps = {
        .addr = 0x08000000U,
        .total_size = FLASH_TOTAL_SIZE,
        .erase_size = 0x1000U,
        .write_gran = 64,
        .erase_size_uniform = true,
        .has_ecc = false,
        .has_write_protect = true,
        .has_crc = false,
    },
    .priv = &g4_priv,
    .initialized = false,
    .lock_cb = NULL,
    .unlock_cb = NULL,
    .lock_depth = 0,
    .event_cb = NULL,
    .event_arg = NULL,
};

#endif /* HAL_FLASH_CHIP_STM32G4 */
