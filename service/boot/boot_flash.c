/**
 * @file    boot_flash.c
 * @brief   Flash 分区管理实现
 */

/* Includes ------------------------------------------------------------------*/
#include "boot_flash.h"

#include <string.h>

#include "crc.h"
#include "drv_stm32g4_flash.h"
#include "log.h"

/* Private constants ---------------------------------------------------------*/

/** @brief 本文件日志开关：置 0 屏蔽本文件全部打印 */
#define BOOT_FLASH_LOG_ENABLE 0

#if BOOT_FLASH_LOG_ENABLE
#define BOOT_FLASH_LOG_E(...) LOG_E("boot_flash", __VA_ARGS__)
#define BOOT_FLASH_LOG_W(...) LOG_W("boot_flash", __VA_ARGS__)
#define BOOT_FLASH_LOG_I(...) LOG_I("boot_flash", __VA_ARGS__)
#define BOOT_FLASH_LOG_D(...) LOG_D("boot_flash", __VA_ARGS__)
#else
#define BOOT_FLASH_LOG_E(...) ((void)0)
#define BOOT_FLASH_LOG_W(...) ((void)0)
#define BOOT_FLASH_LOG_I(...) ((void)0)
#define BOOT_FLASH_LOG_D(...) ((void)0)
#endif

/* Private functions ---------------------------------------------------------*/
static const uint32_t BOOT_FLASH_BOOT_ADDR = BOOT_FLASH_BASE; /**< 0x08000000 */
static const uint32_t BOOT_FLASH_APP_A_ADDR = (BOOT_FLASH_BOOT_ADDR + BOOT_FLASH_BOOT_SIZE); /**< 0x08008000 */
static const uint32_t BOOT_FLASH_APP_B_ADDR = (BOOT_FLASH_APP_A_ADDR + BOOT_FLASH_APP_SIZE); /**< 0x08012000 */
static const uint32_t BOOT_FLASH_META_ADDR = (BOOT_FLASH_APP_B_ADDR + BOOT_FLASH_APP_SIZE); /**< 0x0801C000 */

static inline uint32_t boot_flash_abs_addr(boot_partition_t partition, uint32_t offset)
{
    return boot_flash_partition_addr(partition) + offset;
}

/* Exported functions --------------------------------------------------------*/

uint32_t boot_flash_partition_addr(boot_partition_t partition)
{
    return (partition == BOOT_PARTITION_A) ? BOOT_FLASH_APP_A_ADDR : BOOT_FLASH_APP_B_ADDR;
}

boot_flash_error_t boot_flash_init(boot_flash_context_t* ctx)
{
    if (!ctx) {
        return BOOT_FLASH_ERROR_NULL_PTR;
    }

    ef_port_init();

    /* CRC32 自检: 计算 "123456789" 的 CRC32, 期望 0xCBF43926 */
    {
        const uint8_t test_data[] = "123456789";
        uint32_t test_crc = get_CRC32_check_sum(test_data, sizeof(test_data) - 1U, 0xFFFFFFFFU);
        if (test_crc != 0xCBF43926U) {
            BOOT_FLASH_LOG_E( "CRC32 自检失败: 计算=0x%08lX, 期望=0xCBF43926",
                (unsigned long)test_crc);
        } else {
            BOOT_FLASH_LOG_I( "CRC32 自检通过 (0x%08lX)", (unsigned long)test_crc);
        }
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->initialized = true;
    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_erase_partition(boot_flash_context_t* ctx,
    boot_partition_t partition)
{
    uint32_t addr;
    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (partition > BOOT_PARTITION_B) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }

    addr = boot_flash_partition_addr(partition);
    if (ef_port_erase(addr, BOOT_FLASH_APP_SIZE) != EF_NO_ERR) {
        return BOOT_FLASH_ERROR_ERASE;
    }
    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_write_block(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t offset,
    const uint8_t* data, uint32_t len)
{
    uint32_t addr;
    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!data || len == 0U || partition > BOOT_PARTITION_B) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }
    if ((offset + len) > BOOT_FLASH_APP_SIZE) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }

    addr = boot_flash_abs_addr(partition, offset);
    if (ef_port_write(addr, (const uint32_t*)data, len) != EF_NO_ERR) {
        return BOOT_FLASH_ERROR_WRITE;
    }
    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_verify_block(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t offset,
    const uint8_t* data, uint32_t len)
{
    uint32_t addr;
    uint32_t i;
    const uint8_t* flash_ptr;

    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!data || len == 0U || partition > BOOT_PARTITION_B) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }

    addr = boot_flash_abs_addr(partition, offset);

    /* 确保 D-Cache 中无此地址区的旧数据 */
    ef_port_cache_invalidate();

    flash_ptr = (const uint8_t*)addr;

    /* 逐字节读回比对 */
    for (i = 0U; i < len; i++) {
        if (flash_ptr[i] != data[i]) {
            BOOT_FLASH_LOG_E( "Verify FAIL @ offset=%lu+%lu: flash=0x%02X, expect=0x%02X",
                (unsigned long)offset, (unsigned long)i,
                flash_ptr[i], data[i]);
            BOOT_FLASH_LOG_E( "Verify ctx-8: %02X %02X %02X %02X %02X %02X %02X %02X",
                (i >= 8) ? flash_ptr[i - 8] : 0xFF, (i >= 7) ? flash_ptr[i - 7] : 0xFF,
                (i >= 6) ? flash_ptr[i - 6] : 0xFF, (i >= 5) ? flash_ptr[i - 5] : 0xFF,
                (i >= 4) ? flash_ptr[i - 4] : 0xFF, (i >= 3) ? flash_ptr[i - 3] : 0xFF,
                (i >= 2) ? flash_ptr[i - 2] : 0xFF, (i >= 1) ? flash_ptr[i - 1] : 0xFF);
            BOOT_FLASH_LOG_E( "Verify ctx+8: %02X %02X %02X %02X %02X %02X %02X %02X",
                flash_ptr[i], flash_ptr[i + 1], flash_ptr[i + 2], flash_ptr[i + 3],
                flash_ptr[i + 4], flash_ptr[i + 5], flash_ptr[i + 6], flash_ptr[i + 7]);
            return BOOT_FLASH_ERROR_VERIFY;
        }
    }

    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_read_metadata(boot_flash_context_t* ctx,
    boot_metadata_t* metadata)
{
    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!metadata) {
        return BOOT_FLASH_ERROR_NULL_PTR;
    }

    /* 直接从 Flash 地址读取 Metadata */
    memcpy(metadata, (const void*)BOOT_FLASH_META_ADDR, sizeof(boot_metadata_t));
    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_write_metadata(boot_flash_context_t* ctx,
    const boot_metadata_t* metadata)
{
    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!metadata) {
        return BOOT_FLASH_ERROR_NULL_PTR;
    }

    /* 先擦除 Metadata 页，再写入 */
    if (ef_port_erase(BOOT_FLASH_META_ADDR, BOOT_FLASH_META_SIZE) != EF_NO_ERR) {
        return BOOT_FLASH_ERROR_ERASE;
    }
    if (ef_port_write(BOOT_FLASH_META_ADDR, (const uint32_t*)metadata,
            sizeof(boot_metadata_t))
        != EF_NO_ERR) {
        return BOOT_FLASH_ERROR_WRITE;
    }
    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_compute_crc32(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t size, uint32_t* crc32)
{
    uint32_t addr;
    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!crc32 || partition > BOOT_PARTITION_B
        || size > BOOT_FLASH_APP_SIZE) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }

    addr = boot_flash_partition_addr(partition);

    BOOT_FLASH_LOG_I( "CRC32: part=%c, addr=0x%08lX, size=%lu",
        (partition == BOOT_PARTITION_A) ? 'A' : 'B',
        (unsigned long)addr, (unsigned long)size);

    ef_port_cache_invalidate();

    *crc32 = get_CRC32_check_sum((const uint8_t*)addr, size, 0xFFFFFFFFU);
    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_compute_checksum(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t size, uint32_t* checksum)
{
    uint32_t addr;
    uint32_t sum = 0U;
    uint32_t i;

    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!checksum || partition > BOOT_PARTITION_B
        || size > BOOT_FLASH_APP_SIZE) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }

    addr = boot_flash_partition_addr(partition);

    BOOT_FLASH_LOG_I( "Checksum: part=%c, addr=0x%08lX, size=%lu",
        (partition == BOOT_PARTITION_A) ? 'A' : 'B',
        (unsigned long)addr, (unsigned long)size);

    ef_port_cache_invalidate();

    {
        const volatile uint8_t* p = (const volatile uint8_t*)addr;
        for (i = 0U; i < size; i++) {
            sum += p[i];
        }
    }

    *checksum = sum;
    BOOT_FLASH_LOG_I( "Checksum result: 0x%08lX", (unsigned long)sum);
    return BOOT_FLASH_OK;
}
