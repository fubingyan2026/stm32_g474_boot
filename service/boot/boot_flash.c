/**
 * @file    boot_flash.c
 * @brief   Flash 分区管理实现
 */

/* Includes ------------------------------------------------------------------*/
#include "boot_flash.h"

#include <string.h>

#include "crc.h"
#include "drv_stm32g4_flash.h"

/* Private functions ---------------------------------------------------------*/

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
    flash_ptr = (const uint8_t*)addr;

    /* 逐字节读回比对 */
    for (i = 0U; i < len; i++) {
        if (flash_ptr[i] != data[i]) {
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
            sizeof(boot_metadata_t)) != EF_NO_ERR) {
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
    *crc32 = get_CRC32_check_sum((const uint8_t*)addr, size, 0xFFFFFFFFU);
    return BOOT_FLASH_OK;
}
