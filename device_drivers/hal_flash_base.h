/**
 * @file    hal_flash_base.h
 * @brief   Flash HAL 基础定义 — 芯片驱动与抽象层共享
 *
 * 本文件仅包含类型与接口定义，不声明任何 API 函数。
 * 是 hal_flash 层与芯片驱动层之间的"契约"。
 *
 * 依赖方向:
 *   hal_flash.h  →  hal_flash_base.h  ←  drv_stm32xx_flash.h
 *   (上层 API)       (基础契约)           (底层驱动)
 *
 * 这样芯片驱动只需 include 本文件即可实现 ops 和 dev，
 * 不会引入对上层 API 的依赖。
 */

#ifndef __HAL_FLASH_BASE_H
#define __HAL_FLASH_BASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ====== 错误码 =============================================================*/

/** @brief Flash 操作错误码 */
typedef enum {
    HAL_FLASH_OK = 0,

    /* ---- 硬件操作错误 ---- */
    HAL_FLASH_ERASE_ERR = -1, /**< 擦除失败 (HAL 返回错误) */
    HAL_FLASH_READ_ERR = -2, /**< 读取失败 */
    HAL_FLASH_WRITE_ERR = -3, /**< 编程失败 (含读回校验不匹配) */
    HAL_FLASH_ECC_ERR = -9, /**< ECC 校验错误 (H7 等支持 ECC 的芯片) */

    /* ---- 参数校验错误 ---- */
    HAL_FLASH_PARAM_ERR = -4, /**< 通用参数错误 (兜底) */
    HAL_FLASH_OFFSET_ERR = -5, /**< 偏移超出 Flash 范围 */
    HAL_FLASH_ALIGN_ERR = -6, /**< 地址或大小未按擦除块对齐 */
    HAL_FLASH_SIZE_ERR = -7, /**< size 为 0 或超出剩余空间 */
    HAL_FLASH_NOT_INIT_ERR = -8, /**< 设备未初始化 */

    /* ---- 功能性错误 ---- */
    HAL_FLASH_WP_ERR = -10, /**< 写保护操作失败 */
    HAL_FLASH_OTP_ERR = -11, /**< OTP 操作失败 */
    HAL_FLASH_CRC_ERR = -12, /**< Flash CRC 校验失败 */
} hal_flash_err_t;

/* ====== 能力描述 ===========================================================*/

/** @brief Flash 设备能力描述（只读，可放 Flash 节省 RAM） */
typedef struct {
    uint32_t addr; /**< Flash 物理基地址 */
    uint32_t total_size; /**< Flash 总大小 (字节) */
    uint32_t erase_size; /**< 最小擦除块大小 (字节) */
    uint8_t write_gran; /**< 编程粒度 (位): 8=字节, 64=双字, 256=大页 */
    bool erase_size_uniform; /**< 擦除块大小是否均匀 */
    bool has_ecc; /**< 是否支持 ECC */
    bool has_write_protect; /**< 是否支持扇区级写保护 */
    bool has_crc; /**< 是否支持硬件 CRC 校验 */
} hal_flash_caps_t;

/* ====== 回调类型 ===========================================================*/

/** @brief Flash 事件类型 */
typedef enum {
    HAL_FLASH_EVENT_ERASE_DONE = 1, /**< 异步擦除完成 */
    HAL_FLASH_EVENT_ECC_CORRECTED = 2, /**< ECC 单比特错误已纠正 */
    HAL_FLASH_EVENT_ECC_FATAL = 3, /**< ECC 双比特错误不可恢复 */
} hal_flash_event_t;

/** @brief Flash 事件回调类型 */
typedef void (*hal_flash_event_cb)(hal_flash_event_t event, void* arg);

/** @brief 锁回调函数类型 */
typedef void (*hal_flash_lock_cb)(void);

/* ====== 操作函数表 =========================================================*/

/**
 * @brief Flash 操作函数表 (VTable)
 *
 * 各芯片驱动需实现此接口。offset 参数为相对 Flash 基地址的偏移量。
 * 可选接口可为 NULL，表示当前芯片不支持该功能。
 */
typedef struct {
    hal_flash_err_t (*init)(void);
    hal_flash_err_t (*read)(uint32_t offset, uint8_t* buf, size_t size);
    hal_flash_err_t (*write)(uint32_t offset, const uint8_t* buf, size_t size);
    hal_flash_err_t (*erase)(uint32_t offset, size_t size);
    uint32_t (*erase_size_at)(uint32_t offset); /**< 可选, NULL=均匀 */
    void (*cache_invalidate)(void);
    hal_flash_err_t (*write_protect)(uint32_t offset, size_t size, bool enable); /**< 可选 */
    hal_flash_err_t (*crc_verify)(uint32_t offset, size_t size, uint32_t* crc_out); /**< 可选 */
    hal_flash_err_t (*otp_read)(uint32_t offset, uint8_t* buf, size_t size); /**< 可选 */
    hal_flash_err_t (*otp_write)(uint32_t offset, const uint8_t* buf, size_t size); /**< 可选 */
    hal_flash_err_t (*erase_async)(uint32_t offset, size_t size); /**< 可选 */
} hal_flash_ops_t;

/* ====== 设备实例 ===========================================================*/

/**
 * @brief Flash 设备实例
 *
 * 由各芯片驱动定义具体的 xxx_dev，HAL 层通过 extern 引用。
 */
typedef struct hal_flash_dev {
    const char* name;
    hal_flash_ops_t ops; /**< 内嵌操作函数表 */
    hal_flash_caps_t caps; /**< 能力描述 */
    void* priv; /**< 驱动私有数据 */

    bool initialized; /**< 是否已初始化 */

    /* 锁回调 (由 HAL 层管理) */
    hal_flash_lock_cb lock_cb;
    hal_flash_lock_cb unlock_cb;
    uint32_t lock_depth;

    /* 事件回调 (由 HAL 层管理) */
    hal_flash_event_cb event_cb;
    void* event_arg;
} hal_flash_dev_t;

#ifdef __cplusplus
}
#endif

#endif /* __HAL_FLASH_BASE_H */
