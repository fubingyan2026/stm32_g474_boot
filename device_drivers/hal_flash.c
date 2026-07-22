/**
 * @file    hal_flash.c
 * @brief   Flash 硬件抽象层 — 单例模式实现
 *
 * 编译时通过 HAL_FLASH_CHIP_xxx 宏选择目标芯片驱动，
 * 运行时只有一个 Flash 设备实例，上层 API 无需传 dev 参数。
 */

#include "hal_flash.h"

#include "main.h"
/* ====== 芯片驱动头文件 (编译时选择) ========================================*/

#ifdef HAL_FLASH_CHIP_STM32F4
#include "drv_stm32f4_flash.h"
#elif defined(HAL_FLASH_CHIP_STM32G4)
#include "drv_stm32g4_flash.h"
#elif defined(HAL_FLASH_CHIP_STM32H7)
/* #include "drv_stm32h7_flash.h" — 预留 */
#else
#error "No HAL_FLASH_CHIP_xxx defined"
#endif

/* ====== 单例设备实例 (由芯片驱动定义) ========================================*/

#ifdef HAL_FLASH_CHIP_STM32F4
extern hal_flash_dev_t f4_dev;
#define FLASH_DEV f4_dev
#elif defined(HAL_FLASH_CHIP_STM32G4)
extern hal_flash_dev_t g4_dev;
#define FLASH_DEV g4_dev
#endif

hal_flash_dev_t* hal_flash_dev(void)
{
    return &FLASH_DEV;
}

/* ====== 默认裸机锁 =========================================================*/

static void default_lock(void) { __disable_irq(); }
static void default_unlock(void) { __enable_irq(); }

/* ====== 锁管理 =============================================================*/

void hal_flash_lock(void)
{
    hal_flash_lock_cb lock = FLASH_DEV.lock_cb ? FLASH_DEV.lock_cb : default_lock;
    if (FLASH_DEV.lock_depth == 0) {
        lock();
    }
    FLASH_DEV.lock_depth++;
}

void hal_flash_unlock(void)
{
    hal_flash_lock_cb unlock = FLASH_DEV.unlock_cb ? FLASH_DEV.unlock_cb : default_unlock;
    if (FLASH_DEV.lock_depth > 0) {
        FLASH_DEV.lock_depth--;
    }
    if (FLASH_DEV.lock_depth == 0) {
        unlock();
    }
}

void hal_flash_set_lock_cb(hal_flash_lock_cb lock, hal_flash_lock_cb unlock)
{
    /* 必须成对设置 */
    if ((lock == NULL) != (unlock == NULL)) {
        return;
    }
    hal_flash_lock();
    FLASH_DEV.lock_cb = lock;
    FLASH_DEV.unlock_cb = unlock;
    hal_flash_unlock();
}

/* ====== 参数校验 ============================================================*/

static hal_flash_err_t check_common(uint32_t offset, size_t size)
{
    if (!FLASH_DEV.initialized) {
        return HAL_FLASH_NOT_INIT_ERR;
    }
    if (offset >= FLASH_DEV.caps.total_size) {
        return HAL_FLASH_OFFSET_ERR;
    }
    if (size == 0) {
        return HAL_FLASH_SIZE_ERR;
    }
    if (offset > FLASH_DEV.caps.total_size - size) {
        return HAL_FLASH_SIZE_ERR;
    }
    return HAL_FLASH_OK;
}

/* ====== 公共 API ===========================================================*/

hal_flash_err_t hal_flash_init(void)
{
    if (!FLASH_DEV.ops.init) {
        return HAL_FLASH_PARAM_ERR;
    }

    hal_flash_err_t err = FLASH_DEV.ops.init();
    if (err == HAL_FLASH_OK) {
        FLASH_DEV.initialized = true;
    }
    return err;
}

hal_flash_err_t hal_flash_read(uint32_t offset, uint8_t* buf, size_t size)
{
    if (!buf) {
        return HAL_FLASH_PARAM_ERR;
    }
    if (!FLASH_DEV.ops.read) {
        return HAL_FLASH_PARAM_ERR;
    }

    hal_flash_err_t err = check_common(offset, size);
    if (err != HAL_FLASH_OK) {
        return err;
    }
    return FLASH_DEV.ops.read(offset, buf, size);
}

hal_flash_err_t hal_flash_write(uint32_t offset, const uint8_t* buf, size_t size)
{
    if (!buf) {
        return HAL_FLASH_PARAM_ERR;
    }
    if (!FLASH_DEV.ops.write) {
        return HAL_FLASH_PARAM_ERR;
    }

    hal_flash_err_t err = check_common(offset, size);
    if (err != HAL_FLASH_OK) {
        return err;
    }

    hal_flash_lock();
    err = FLASH_DEV.ops.write(offset, buf, size);
    hal_flash_unlock();
    return err;
}

hal_flash_err_t hal_flash_erase(uint32_t offset, size_t size)
{
    if (!FLASH_DEV.ops.erase) {
        return HAL_FLASH_PARAM_ERR;
    }

    hal_flash_err_t err = check_common(offset, size);
    if (err != HAL_FLASH_OK) {
        return err;
    }

    /* 对齐检查 */
    uint32_t blk = hal_flash_erase_size_at(offset);
    if (blk == 0 || (offset % blk) != 0 || (size % blk) != 0) {
        return HAL_FLASH_ALIGN_ERR;
    }

    hal_flash_lock();
    err = FLASH_DEV.ops.erase(offset, size);
    hal_flash_unlock();
    return err;
}

void hal_flash_cache_invalidate(void)
{
    if (FLASH_DEV.ops.cache_invalidate) {
        FLASH_DEV.ops.cache_invalidate();
    }
}

uint32_t hal_flash_erase_size_at(uint32_t offset)
{
    if (FLASH_DEV.ops.erase_size_at) {
        return FLASH_DEV.ops.erase_size_at(offset);
    }
    return FLASH_DEV.caps.erase_size;
}

hal_flash_err_t hal_flash_write_protect(uint32_t offset, size_t size, bool enable)
{
    if (!FLASH_DEV.caps.has_write_protect || !FLASH_DEV.ops.write_protect) {
        return HAL_FLASH_PARAM_ERR;
    }
    hal_flash_err_t err = check_common(offset, size);
    if (err != HAL_FLASH_OK) {
        return err;
    }
    return FLASH_DEV.ops.write_protect(offset, size, enable);
}

hal_flash_err_t hal_flash_crc_verify(uint32_t offset, size_t size, uint32_t* crc_out)
{
    if (!crc_out || !FLASH_DEV.caps.has_crc || !FLASH_DEV.ops.crc_verify) {
        return HAL_FLASH_PARAM_ERR;
    }
    hal_flash_err_t err = check_common(offset, size);
    if (err != HAL_FLASH_OK) {
        return err;
    }
    return FLASH_DEV.ops.crc_verify(offset, size, crc_out);
}

hal_flash_err_t hal_flash_otp_read(uint32_t offset, uint8_t* buf, size_t size)
{
    if (!buf || !FLASH_DEV.ops.otp_read) {
        return HAL_FLASH_PARAM_ERR;
    }
    return FLASH_DEV.ops.otp_read(offset, buf, size);
}

hal_flash_err_t hal_flash_otp_write(uint32_t offset, const uint8_t* buf, size_t size)
{
    if (!buf || !FLASH_DEV.ops.otp_write) {
        return HAL_FLASH_PARAM_ERR;
    }
    return FLASH_DEV.ops.otp_write(offset, buf, size);
}

hal_flash_err_t hal_flash_erase_async(uint32_t offset, size_t size)
{
    if (!FLASH_DEV.ops.erase_async) {
        return HAL_FLASH_PARAM_ERR;
    }
    hal_flash_err_t err = check_common(offset, size);
    if (err != HAL_FLASH_OK) {
        return err;
    }
    uint32_t blk = hal_flash_erase_size_at(offset);
    if (blk == 0 || (offset % blk) != 0 || (size % blk) != 0) {
        return HAL_FLASH_ALIGN_ERR;
    }
    return FLASH_DEV.ops.erase_async(offset, size);
}

void hal_flash_set_event_cb(hal_flash_event_cb cb, void* arg)
{
    hal_flash_lock();
    FLASH_DEV.event_cb = cb;
    FLASH_DEV.event_arg = arg;
    hal_flash_unlock();
}

const hal_flash_caps_t* hal_flash_get_caps(void)
{
    return &FLASH_DEV.caps;
}

const char* hal_flash_err_str(hal_flash_err_t err)
{
    switch (err) {
    case HAL_FLASH_OK:            return "成功";
    case HAL_FLASH_ERASE_ERR:     return "擦除失败";
    case HAL_FLASH_READ_ERR:      return "读取失败";
    case HAL_FLASH_WRITE_ERR:     return "写入失败";
    case HAL_FLASH_PARAM_ERR:     return "参数错误";
    case HAL_FLASH_OFFSET_ERR:    return "偏移越界";
    case HAL_FLASH_ALIGN_ERR:     return "地址/大小未对齐";
    case HAL_FLASH_SIZE_ERR:      return "长度非法";
    case HAL_FLASH_NOT_INIT_ERR:  return "设备未初始化";
    case HAL_FLASH_ECC_ERR:       return "ECC 校验错误";
    case HAL_FLASH_WP_ERR:        return "写保护操作失败";
    case HAL_FLASH_OTP_ERR:       return "OTP 操作失败";
    case HAL_FLASH_CRC_ERR:       return "CRC 校验失败";
    default:                      return "未知错误";
    }
}
