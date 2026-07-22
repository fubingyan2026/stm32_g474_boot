/**
 * @file    boot_flash.c
 * @brief   Flash 分区管理实现
 */

/* Includes ------------------------------------------------------------------*/
#include "boot_flash.h"

#include <string.h>

#include "crc.h"
#include "hal_flash.h"
#include "log.h"
#include "ring_storage.h"

/* Private constants ---------------------------------------------------------*/

/** @brief 本文件日志开关：置 0 屏蔽本文件全部打印 */
#define BOOT_FLASH_LOG_ENABLE 1

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
static inline uint32_t boot_flash_base(void)
{
    return hal_flash_get_caps()->addr;
}

static inline uint32_t boot_flash_abs_addr(boot_partition_t partition, uint32_t offset)
{
    return boot_flash_partition_addr(partition) + offset;
}

/* Exported functions --------------------------------------------------------*/

uint32_t boot_flash_partition_addr(boot_partition_t partition)
{
    const uint32_t base = boot_flash_base();
    return (partition == BOOT_PARTITION_A)
        ? (base + BOOT_FLASH_BOOT_SIZE)
        : (base + BOOT_FLASH_BOOT_SIZE + BOOT_FLASH_APP_SIZE);
}

boot_flash_error_t boot_flash_init(boot_flash_context_t* ctx)
{
    ring_storage_error_t rs_err;
    uint32_t page_size;

    if (!ctx) {
        return BOOT_FLASH_ERROR_NULL_PTR;
    }

    {
        hal_flash_err_t flash_err = hal_flash_init();
        if (flash_err != HAL_FLASH_OK) {
            BOOT_FLASH_LOG_E("hal_flash 初始化失败: %s (err=%d)",
                hal_flash_err_str(flash_err), flash_err);
            return BOOT_FLASH_ERROR_ERASE;
        }
    }
    page_size = hal_flash_get_caps()->erase_size;

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
            .start_addr        = boot_flash_base() + BOOT_FLASH_BOOT_SIZE + BOOT_FLASH_APP_SIZE * 2,
            .area_size         = BOOT_FLASH_META_SIZE,
            .sector_size       = page_size,
            .write_gran        = RING_STORAGE_WRITE_GRAN_64,
            .frame_buffer      = ctx->meta_frame_buf,
            .frame_buffer_size = sizeof(ctx->meta_frame_buf),
        };

        rs_err = ring_storage_init(&ctx->meta_storage, &cfg);
        if (rs_err != RING_STORAGE_OK && rs_err != RING_STORAGE_ERROR_NO_VALID_FRAME) {
            BOOT_FLASH_LOG_E( "ring_storage 初始化失败: err=%d", rs_err);
            return BOOT_FLASH_ERROR_ERASE;
        }

        /* 注册 metadata 结构体为一个 KV */
        rs_err = ring_storage_register(&ctx->meta_storage, "meta",
            &ctx->meta_cache, sizeof(ctx->meta_cache));
        if (rs_err != RING_STORAGE_OK) {
            BOOT_FLASH_LOG_E( "ring_storage 注册失败: err=%d", rs_err);
            return BOOT_FLASH_ERROR_ERASE;
        }

        /* 尝试加载 */
        rs_err = ring_storage_load(&ctx->meta_storage);
    }

    /* 更新上电启动次数，始终保存以确保持久化。
     * 单次 save 开销 ~200μs（追加写入，无 GC 时无需擦除），
     * GC 约每 40 帧触发一次（擦除 4KB ~16ms），分摊后每次 save 约 +400μs。
     * 以 STM32G4 Flash 10000 次擦写寿命计算，可支持 ~400000 次启动。 */
    {
        if (rs_err == RING_STORAGE_ERROR_NO_VALID_FRAME) {
            ctx->meta_cache.magic = BOOT_METADATA_MAGIC;
        }
        ctx->meta_cache.reboot_counts++;
        ring_storage_save(&ctx->meta_storage);
    }

    ctx->initialized = true;

    /* 初始化完成后打印 Metadata 概览 */
    BOOT_FLASH_LOG_I( "初始化: rs版本=%u, 启动次数=%u, 分区=%c, 固件版本=%u, 大小=%u, 校验和=0x%08lX",
        ctx->meta_storage.latest_version,
        (unsigned int)ctx->meta_cache.reboot_counts,
        'A' + ctx->meta_cache.boot_partition,
        ctx->meta_cache.version,
        (unsigned int)ctx->meta_cache.fw_size,
        (unsigned long)ctx->meta_cache.fw_checksum);
    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_erase_partition(boot_flash_context_t* ctx,
    boot_partition_t partition)
{
    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (partition > BOOT_PARTITION_B) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }

    {
        const uint32_t offset = boot_flash_partition_addr(partition) - boot_flash_base();
        hal_flash_err_t flash_err = hal_flash_erase(offset, BOOT_FLASH_APP_SIZE);
        if (flash_err != HAL_FLASH_OK) {
            BOOT_FLASH_LOG_E("分区%c 擦除失败: %s (err=%d)",
                (partition == BOOT_PARTITION_A) ? 'A' : 'B',
                hal_flash_err_str(flash_err), flash_err);
            return BOOT_FLASH_ERROR_ERASE;
        }
    }
    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_write_block(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t offset,
    const uint8_t* data, uint32_t len)
{
    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!data || len == 0U || partition > BOOT_PARTITION_B) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }
    if ((offset + len) > BOOT_FLASH_APP_SIZE) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }

    {
        const uint32_t flash_off = boot_flash_abs_addr(partition, offset) - boot_flash_base();
        hal_flash_err_t flash_err = hal_flash_write(flash_off, data, len);
        if (flash_err != HAL_FLASH_OK) {
            BOOT_FLASH_LOG_E("分区%c 写入失败: %s (err=%d), offset=%lu, len=%lu",
                (partition == BOOT_PARTITION_A) ? 'A' : 'B',
                hal_flash_err_str(flash_err), flash_err,
                (unsigned long)offset, (unsigned long)len);
            return BOOT_FLASH_ERROR_WRITE;
        }
    }
    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_verify_block(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t offset,
    const uint8_t* data, uint32_t len)
{
    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!data || len == 0U || partition > BOOT_PARTITION_B) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }

    /* 确保 D-Cache 中无此地址区的旧数据 */
    hal_flash_cache_invalidate();

    const uint8_t* flash_ptr = (const uint8_t*)boot_flash_abs_addr(partition, offset);

    /* 逐字节读回比对 */
    for (uint32_t i = 0U; i < len; i++) {
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
        BOOT_FLASH_LOG_E( "ring_storage 加载失败: err=%d", rs_err);
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

    /* 复制到 ring_storage 绑定的缓存（保留 reboot_counts 不被覆盖） */
    {
        const uint32_t saved_reboot = ctx->meta_cache.reboot_counts;
        memcpy(&ctx->meta_cache, metadata, sizeof(ctx->meta_cache));
        ctx->meta_cache.reboot_counts = saved_reboot;
    }
    rs_err = ring_storage_save(&ctx->meta_storage);
    if (rs_err != RING_STORAGE_OK) {
        BOOT_FLASH_LOG_E( "ring_storage 保存失败: err=%d", rs_err);
        return BOOT_FLASH_ERROR_WRITE;
    }

    BOOT_FLASH_LOG_I( "Metadata 写入: rs版本=%u, 启动次数=%u, 分区=%c, 固件版本=%u, 校验和=0x%08lX",
        ctx->meta_storage.latest_version,
        (unsigned int)ctx->meta_cache.reboot_counts,
        'A' + metadata->boot_partition,
        metadata->version,
        (unsigned long)metadata->fw_checksum);

    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_compute_crc32(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t size, uint32_t* crc32)
{
    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!crc32 || partition > BOOT_PARTITION_B
        || size > BOOT_FLASH_APP_SIZE) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }

    const uint32_t addr = boot_flash_partition_addr(partition);

    BOOT_FLASH_LOG_I( "CRC32: 分区=%c, 地址=0x%08lX, 大小=%lu",
        (partition == BOOT_PARTITION_A) ? 'A' : 'B',
        (unsigned long)addr, (unsigned long)size);

    hal_flash_cache_invalidate();

    *crc32 = get_CRC32_check_sum((const uint8_t*)addr, size, 0xFFFFFFFFU);
    return BOOT_FLASH_OK;
}

boot_flash_error_t boot_flash_compute_checksum(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t size, uint32_t* checksum)
{
    if (!ctx || !ctx->initialized) {
        return BOOT_FLASH_ERROR_UNINITIALIZED;
    }
    if (!checksum || partition > BOOT_PARTITION_B
        || size > BOOT_FLASH_APP_SIZE) {
        return BOOT_FLASH_ERROR_INVALID_PARAM;
    }

    const uint32_t addr = boot_flash_partition_addr(partition);

    BOOT_FLASH_LOG_I( "累加和: 分区=%c, 地址=0x%08lX, 大小=%lu",
        (partition == BOOT_PARTITION_A) ? 'A' : 'B',
        (unsigned long)addr, (unsigned long)size);

    hal_flash_cache_invalidate();

    uint32_t sum = 0U;
    {
        const volatile uint8_t* p = (const volatile uint8_t*)addr;
        for (uint32_t i = 0U; i < size; i++) {
            sum += p[i];
        }
    }

    *checksum = sum;
    BOOT_FLASH_LOG_I( "累加和结果: 0x%08lX", (unsigned long)sum);
    return BOOT_FLASH_OK;
}
