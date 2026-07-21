/**
 * @file    boot_flash.h
 * @brief   Flash 分区管理 — 双 A/B 分区 + Metadata 页
 * @attention
 *
 * 封装 drv_stm32g4_flash，提供分区维度的擦除/写入/校验操作。
 * 所有地址和大小以宏定义，可根据实际 bootloader 大小调整。
 */

#ifndef __BOOT_FLASH_H
#define __BOOT_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

#include "ring_storage.h"

/* Exported constants --------------------------------------------------------*/

/** Flash 物理参数 */
#define BOOT_FLASH_BASE         0x08000000U  /**< Flash 起始地址 */
#define BOOT_FLASH_TOTAL_SIZE   0x20000U     /**< 128 KB */

/** 分区布局 */
#define BOOT_FLASH_BOOT_SIZE    0x9000U      /**< Bootloader: 36 KB */
#define BOOT_FLASH_APP_SIZE     0x9000U      /**< App 分区:  36 KB */
#define BOOT_FLASH_META_SIZE    0x2000U      /**< Metadata: 8 KB (ring_storage 需要 ≥2 扇区) */

/** Metadata 魔数 */
#define BOOT_METADATA_MAGIC     0x424F4F54U  /**< "BOOT" */
#define BOOT_META_FRAME_BUF_SIZE  128U       /**< ring_storage 帧缓冲区 */

/* Exported types ------------------------------------------------------------*/

/** 引导分区标识 */
typedef enum {
    BOOT_PARTITION_A = 0U, /**< App A 分区 */
    BOOT_PARTITION_B = 1U, /**< App B 分区 */
} boot_partition_t;

/** 引导 Metadata 结构体 */
typedef struct {
    uint32_t magic;            /**< 魔数，标识有效 metadata */
    uint8_t  boot_partition;   /**< 当前引导分区 */
    uint8_t  upgrade_flag;     /**< 升级标志：0=正常，1=升级中，2=升级完成待验证 */
    uint16_t version;          /**< 固件版本号 */
    uint32_t fw_size;          /**< 固件大小 */
    uint32_t fw_checksum;      /**< 固件校验和 */
    uint32_t reserved[2];      /**< 预留 */
} boot_metadata_t;

/** Flash 操作错误码 */
typedef enum {
    BOOT_FLASH_OK = 0,              /**< 操作成功 */
    BOOT_FLASH_ERROR_NULL_PTR,      /**< 空指针 */
    BOOT_FLASH_ERROR_UNINITIALIZED, /**< 未初始化 */
    BOOT_FLASH_ERROR_INVALID_PARAM, /**< 无效参数 */
    BOOT_FLASH_ERROR_ERASE,         /**< 擦除失败 */
    BOOT_FLASH_ERROR_WRITE,         /**< 写入失败 */
    BOOT_FLASH_ERROR_VERIFY,        /**< 校验失败 */
} boot_flash_error_t;

/** Flash 分区管理上下文 */
typedef struct boot_flash_context boot_flash_context_t;

struct boot_flash_context {
    bool initialized;                          /**< 初始化标志 */
    ring_storage_context_t meta_storage;       /**< ring_storage 实例 */
    uint8_t meta_frame_buf[BOOT_META_FRAME_BUF_SIZE]; /**< 帧缓冲区 */
    boot_metadata_t meta_cache;                /**< Metadata 缓存 */
};

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化 Flash 分区管理器
 * @param ctx 上下文指针
 * @return 操作结果
 */
boot_flash_error_t boot_flash_init(boot_flash_context_t* ctx);

/**
 * @brief 擦除指定分区
 * @param ctx 上下文指针
 * @param partition 分区标识
 * @return 操作结果
 */
boot_flash_error_t boot_flash_erase_partition(boot_flash_context_t* ctx,
    boot_partition_t partition);

/**
 * @brief 写入 1KB 数据块到指定分区的偏移位置
 * @param ctx 上下文指针
 * @param partition 分区标识
 * @param offset 分区内偏移（字节）
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return 操作结果
 */
boot_flash_error_t boot_flash_write_block(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t offset,
    const uint8_t* data, uint32_t len);

/**
 * @brief 读回比对已写入的 1KB 数据块
 * @param ctx 上下文指针
 * @param partition 分区标识
 * @param offset 分区内偏移（字节）
 * @param data 期望数据
 * @param len 数据长度
 * @return 操作结果
 */
boot_flash_error_t boot_flash_verify_block(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t offset,
    const uint8_t* data, uint32_t len);

/**
 * @brief 读取 Metadata 页
 * @param ctx 上下文指针
 * @param metadata 输出：Metadata 结构体
 * @return 操作结果
 */
boot_flash_error_t boot_flash_read_metadata(boot_flash_context_t* ctx,
    boot_metadata_t* metadata);

/**
 * @brief 写入 Metadata 页
 * @param ctx 上下文指针
 * @param metadata Metadata 结构体指针
 * @return 操作结果
 */
boot_flash_error_t boot_flash_write_metadata(boot_flash_context_t* ctx,
    const boot_metadata_t* metadata);

/**
 * @brief 计算指定分区的 CRC32
 * @param ctx 上下文指针
 * @param partition 分区标识
 * @param size 数据大小
 * @param crc32 输出：CRC32 值
 * @return 操作结果
 */
boot_flash_error_t boot_flash_compute_crc32(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t size, uint32_t* crc32);

/**
 * @brief 计算指定分区的 32-bit 累加和校验
 * @param ctx 上下文指针
 * @param partition 分区标识
 * @param size 数据大小
 * @param checksum 输出：32-bit 累加和
 * @return 操作结果
 */
boot_flash_error_t boot_flash_compute_checksum(boot_flash_context_t* ctx,
    boot_partition_t partition, uint32_t size, uint32_t* checksum);

/**
 * @brief 获取分区起始地址
 * @param partition 分区标识
 * @return Flash 地址
 */
uint32_t boot_flash_partition_addr(boot_partition_t partition);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_FLASH_H */
