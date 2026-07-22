/**
 * @file    drv_stm32f4_flash.c
 * @brief   STM32F407 Flash 底层驱动 — 适配 hal_flash 抽象层
 *
 * F407 共 12 个扇区（1MB）：
 *   扇区 0-3:  各 16KB
 *   扇区 4:    64KB
 *   扇区 5-11: 各 128KB
 *
 * 实现 hal_flash_ops_t 接口，导出 f4_ops 和 f4_sectors 供 HAL 层引用。
 * 仅在定义了 HAL_FLASH_CHIP_STM32F4 时编译，否则本文件为空。
 */

#include "hal_flash.h"

#ifdef HAL_FLASH_CHIP_STM32F4

/* Includes ------------------------------------------------------------------*/
#include "drv_stm32f4_flash.h"
#include "log.h"

#include "stm32f4xx_hal.h"

#include <string.h>

/* 模块日志开关 ----------------------------------------------------------------*/

/** @brief 本文件日志开关：置 0 屏蔽本文件全部打印 */
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

/** @brief F4 错误标志组合（F4 无 FLASH_FLAG_ALL_ERRORS 宏） */
#define DRV_FLASH_FLAG_ALL_ERRORS (FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR)

/* Private types -------------------------------------------------------------*/

/** @brief 扇区描述：起始地址 + 大小 */
typedef struct {
    uint32_t base; /**< 扇区起始地址 */
    uint32_t size; /**< 扇区大小（字节） */
} f4_sector_desc_t;

/* Private variables ---------------------------------------------------------*/

static uint64_t s_write_buf; /* Flash 写入临时缓冲区 (64-bit 对齐) */
static uint64_t s_read_buf; /* Flash 读取校验缓冲区 (64-bit 对齐) */

/* Private const -------------------------------------------------------------*/

static const uint32_t FLASH_PROGRAM_SIZE = 8; /* 编程粒度: 64-bit 双字 */
static const uint64_t FLASH_ERASED_VAL = (~0ULL); /* Flash 擦除后默认值 */
static const uint32_t FLASH_BASE_ADDR = 0x08000000U; /* F4 Flash 基地址 */
static const uint32_t FLASH_TOTAL_SIZE = 0x00100000U; /* 1 MB */

/** @brief STM32F407VG 扇区表（1MB Flash，12 扇区） */
const f4_sector_desc_t f4_sectors[] = {
    { 0x08000000U, 0x4000U }, /* 扇区  0:  16 KB */
    { 0x08004000U, 0x4000U }, /* 扇区  1:  16 KB */
    { 0x08008000U, 0x4000U }, /* 扇区  2:  16 KB */
    { 0x0800C000U, 0x4000U }, /* 扇区  3:  16 KB */
    { 0x08010000U, 0x10000U }, /* 扇区  4:  64 KB */
    { 0x08020000U, 0x20000U }, /* 扇区  5: 128 KB */
    { 0x08040000U, 0x20000U }, /* 扇区  6: 128 KB */
    { 0x08060000U, 0x20000U }, /* 扇区  7: 128 KB */
    { 0x08080000U, 0x20000U }, /* 扇区  8: 128 KB */
    { 0x080A0000U, 0x20000U }, /* 扇区  9: 128 KB */
    { 0x080C0000U, 0x20000U }, /* 扇区 10: 128 KB */
    { 0x080E0000U, 0x20000U }, /* 扇区 11: 128 KB */
};

static const uint32_t FLASH_SECTOR_COUNT = sizeof(f4_sectors) / sizeof(f4_sectors[0]);

/* Private function prototypes -----------------------------------------------*/

static uint32_t find_sector(uint32_t addr)
{
    for (uint32_t i = 0; i < FLASH_SECTOR_COUNT; i++) {
        if (addr >= f4_sectors[i].base && addr < f4_sectors[i].base + f4_sectors[i].size) {
            return i;
        }
    }
    return FLASH_SECTOR_COUNT;
}

/* ====== hal_flash_ops_t 接口实现 ========================================== */

static hal_flash_err_t f4_init(void)
{
    FLASH_LOG_I("初始化: F407 Flash, %lu 个扇区, 总容量=%luKB",
        (unsigned long)FLASH_SECTOR_COUNT,
        (unsigned long)(FLASH_TOTAL_SIZE >> 10));
    return HAL_FLASH_OK;
}

static hal_flash_err_t f4_read(uint32_t offset, uint8_t* buf, size_t size)
{
    uint32_t addr = FLASH_BASE_ADDR + offset;
    uint8_t* dst = buf;

    for (size_t i = 0; i < size; i++, addr++, dst++) {
        *dst = *(volatile uint8_t*)addr;
    }

    return HAL_FLASH_OK;
}

static hal_flash_err_t f4_erase(uint32_t offset, size_t size)
{
    hal_flash_err_t result = HAL_FLASH_OK;
    uint32_t error = 0;

    uint32_t addr = FLASH_BASE_ADDR + offset;
    uint32_t start_sector = find_sector(addr);
    uint32_t end_addr = addr + size;
    uint32_t end_sector = (size > 0) ? find_sector(end_addr - 1) : FLASH_SECTOR_COUNT;

    if (start_sector >= FLASH_SECTOR_COUNT || end_sector >= FLASH_SECTOR_COUNT) {
        FLASH_LOG_E("擦除: 偏移越界, addr=0x%08lX, end=0x%08lX",
            (unsigned long)addr, (unsigned long)end_addr);
        return HAL_FLASH_OFFSET_ERR;
    }
    if (addr != f4_sectors[start_sector].base) {
        FLASH_LOG_E("擦除: 起始地址未扇区对齐, addr=0x%08lX, sector=%lu base=0x%08lX",
            (unsigned long)addr, (unsigned long)start_sector,
            (unsigned long)f4_sectors[start_sector].base);
        return HAL_FLASH_ALIGN_ERR;
    }
    if (end_addr != f4_sectors[end_sector].base + f4_sectors[end_sector].size) {
        FLASH_LOG_E("擦除: 结束地址未扇区对齐, end=0x%08lX, sector=%lu end=0x%08lX",
            (unsigned long)end_addr, (unsigned long)end_sector,
            (unsigned long)(f4_sectors[end_sector].base + f4_sectors[end_sector].size));
        return HAL_FLASH_ALIGN_ERR;
    }

    uint32_t nb_sectors = end_sector - start_sector + 1;

    FLASH_LOG_I("擦除: addr=0x%08lX, size=%lu, 扇区=%lu..%lu (%lu)",
        (unsigned long)addr, (unsigned long)size,
        (unsigned long)start_sector, (unsigned long)end_sector,
        (unsigned long)nb_sectors);

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(DRV_FLASH_FLAG_ALL_ERRORS);

    FLASH_EraseInitTypeDef erase_init = {
        .TypeErase = FLASH_TYPEERASE_SECTORS,
        .Banks = FLASH_BANK_1,
        .Sector = start_sector,
        .NbSectors = nb_sectors,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
    };

    if (HAL_FLASHEx_Erase(&erase_init, &error) != HAL_OK) {
        FLASH_LOG_E("擦除错误: addr=0x%08lX, sector=%lu, HAL_Err=0x%08lX",
            (unsigned long)addr, (unsigned long)error,
            (unsigned long)HAL_FLASH_GetError());
        result = HAL_FLASH_ERASE_ERR;
    }

    HAL_FLASH_Lock();
    return result;
}

static hal_flash_err_t f4_write(uint32_t offset, const uint8_t* buf, size_t size)
{
    hal_flash_err_t result = HAL_FLASH_OK;
    uint32_t addr = FLASH_BASE_ADDR + offset;
    const uint8_t* src = buf;

    FLASH_LOG_I("写入: addr=0x%08lX, size=%lu",
        (unsigned long)addr, (unsigned long)size);

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(DRV_FLASH_FLAG_ALL_ERRORS);

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
                FLASH_LOG_E("Flash 编程错误: i=%u, addr=0x%08lX, HAL_Error=0x%08lX",
                    (unsigned)i, (unsigned long)addr,
                    (unsigned long)HAL_FLASH_GetError());
                result = HAL_FLASH_WRITE_ERR;
                goto exit_write;
            }
        }

        s_read_buf = *(volatile uint64_t*)addr;
        if (s_read_buf != s_write_buf) {
            FLASH_LOG_E("Flash 读回不匹配: i=%u, addr=0x%08lX, "
                        "written=0x%016llX, readback=0x%016llX",
                (unsigned)i, (unsigned long)addr,
                (unsigned long long)s_write_buf,
                (unsigned long long)s_read_buf);
            result = HAL_FLASH_WRITE_ERR;
            goto exit_write;
        }

        addr += FLASH_PROGRAM_SIZE;
        src += copy_len;
    }

exit_write:
    HAL_FLASH_Lock();
    return result;
}

static void f4_cache_invalidate(void)
{
    __HAL_FLASH_PREFETCH_BUFFER_DISABLE();
    __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
}

static uint32_t f4_erase_size_at(uint32_t offset)
{
    uint32_t addr = FLASH_BASE_ADDR + offset;
    uint32_t idx = find_sector(addr);

    if (idx < FLASH_SECTOR_COUNT) {
        return f4_sectors[idx].size;
    }
    return 0;
}

/* ====== 设备实例导出 ======================================================== */

hal_flash_dev_t f4_dev = {
    .name = "stm32f4",
    .ops = {
        .init = f4_init,
        .read = f4_read,
        .write = f4_write,
        .erase = f4_erase,
        .erase_size_at = f4_erase_size_at,
        .cache_invalidate = f4_cache_invalidate,
    },
    .caps = {
        .addr = 0x08000000U,
        .total_size = 0x00100000U,
        .erase_size = 0x4000U,
        .write_gran = 64,
        .erase_size_uniform = false,
        .has_ecc = false,
        .has_write_protect = false,
        .has_crc = false,
    },
    .priv = (void*)f4_sectors,
    .initialized = false,
    .lock_cb = NULL,
    .unlock_cb = NULL,
    .lock_depth = 0,
    .event_cb = NULL,
    .event_arg = NULL,
};

#endif /* HAL_FLASH_CHIP_STM32F4 */
