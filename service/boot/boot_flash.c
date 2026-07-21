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
#include "ring_storage.h"

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
static const uint32_t BOOT_FLASH_META_ADDR = (BOOT_FLASH_APP_B_ADDR + BOOT_FLASH_APP_SIZE); /**< 0x0801B000 */

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
    ring_storage_error_t rs_err;
    uint32_t page_size;

    if (!ctx) {
        return BOOT_FLASH_ERROR_NULL_PTR;
    }

    ef_port_init();
    page_size = ef_port_get_page_size();

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

    /* 初始化 ring_storage 管理 Metadata 区域 */
    {
        const ring_storage_config_t cfg = {
            .start_addr        = BOOT_FLASH_META_ADDR,
            .area_size         = BOOT_FLASH_META_SIZE,
            .sector_size       = page_size,
            .write_gran        = RING_STORAGE_WRITE_GRAN_64,
            .frame_buffer      = ctx->meta_frame_buf,
            .frame_buffer_size = sizeof(ctx->meta_frame_buf),
        };

        rs_err = ring_storage_init(&ctx->meta_storage, &cfg);
        if (rs_err != RING_STORAGE_OK && rs_err != RING_STORAGE_ERROR_NO_VALID_FRAME) {
            BOOT_FLASH_LOG_E( "ring_storage init 失败: err=%d", rs_err);
            return BOOT_FLASH_ERROR_ERASE;
        }

        /* 注册 metadata 结构体为一个 KV */
        rs_err = ring_storage_register(&ctx->meta_storage, "meta",
            &ctx->meta_cache, sizeof(ctx->meta_cache));
        if (rs_err != RING_STORAGE_OK) {
            BOOT_FLASH_LOG_E( "ring_storage register 失败: err=%d", rs_err);
            return BOOT_FLASH_ERROR_ERASE;
        }

        /* 尝试加载，无有效帧时尝试从旧格式迁移 */
        rs_err = ring_storage_load(&ctx->meta_storage);
        if (rs_err == RING_STORAGE_ERROR_NO_VALID_FRAME) {
            /* 旧格式迁移：检查原 4KB Metadata 位置是否有 raw 数据 */
            const boot_metadata_t* old_meta =
                (const boot_metadata_t*)BOOT_FLASH_META_ADDR;
            if (old_meta->magic == BOOT_METADATA_MAGIC) {
                BOOT_FLASH_LOG_I( "检测到旧格式 Metadata，迁移至 ring_storage");
                memcpy(&ctx->meta_cache, old_meta, sizeof(ctx->meta_cache));
                ring_storage_save(&ctx->meta_storage);
                rs_err = RING_STORAGE_OK;
            }
        }
    }

    ctx->initialized = true;

    /* 初始化完成后打印 Metadata 概览 */
    if (rs_err == RING_STORAGE_OK) {
        BOOT_FLASH_LOG_I( "Init: ring_storage ver=%u, meta: part=%c, fw_ver=%u, size=%u, cs=0x%08lX",
            ctx->meta_storage.latest_version,
            'A' + ctx->meta_cache.boot_partition,
            ctx->meta_cache.version,
            (unsigned int)ctx->meta_cache.fw_size,
            (unsigned long)ctx->meta_cache.fw_checksum);
    } else {
        BOOT_FLASH_LOG_I( "Init: ring_storage ver=%u, 无有效 Metadata",
            ctx->meta_storage.latest_version);
    }
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
            return BOOT_FLASH_ERROR_VERIFY;
        }
    }

    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_read_metadata(boot_flash_context_t* ctx,
    boot_metadata_t* metadata)
{
    ring_storage_error_t rs_err;

    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!metadata) {
        return BOOT_FLASH_ERROR_NULL_PTR;
    }

    rs_err = ring_storage_load(&ctx->meta_storage);
    if (rs_err == RING_STORAGE_ERROR_NO_VALID_FRAME) {
        /* 无有效帧，返回空结构体（magic 不匹配则上层会进入 bootloader） */
        memset(metadata, 0, sizeof(*metadata));
        return BOOT_FLASH_OK;
    }
    if (rs_err != RING_STORAGE_OK) {
        BOOT_FLASH_LOG_E( "ring_storage load 失败: err=%d", rs_err);
        return BOOT_FLASH_ERROR_VERIFY;
    }

    /* 从 ring_storage 缓存复制到调用者 buffer */
    memcpy(metadata, &ctx->meta_cache, sizeof(*metadata));
    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_write_metadata(boot_flash_context_t* ctx,
    const boot_metadata_t* metadata)
{
    ring_storage_error_t rs_err;

    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!metadata) {
        return BOOT_FLASH_ERROR_NULL_PTR;
    }

    /* 复制到 ring_storage 绑定的缓存，然后原子保存 */
    memcpy(&ctx->meta_cache, metadata, sizeof(ctx->meta_cache));
    rs_err = ring_storage_save(&ctx->meta_storage);
    if (rs_err != RING_STORAGE_OK) {
        BOOT_FLASH_LOG_E( "ring_storage save 失败: err=%d", rs_err);
        return BOOT_FLASH_ERROR_WRITE;
    }

    BOOT_FLASH_LOG_I( "Metadata 写入成功: ring_storage ver=%u, part=%c, fw_ver=%u, cs=0x%08lX",
        ctx->meta_storage.latest_version,
        'A' + metadata->boot_partition,
        metadata->version,
        (unsigned long)metadata->fw_checksum);

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
