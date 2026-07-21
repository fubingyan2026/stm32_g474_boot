/**
 * @file    ring_storage.h
 * @brief   环形缓冲区 Flash 参数存储模块
 * @author  FOC Development Team
 * @version V1.0.0
 * @date    2026-07-21
 *
 * @note    专为嵌入式 MCU 参数持久化设计的 Flash 存储模块。
 *          采用环形缓冲区 + 整帧原子提交设计，支持磨损均衡和断电保护。
 *
 * @par 核心特性
 *          - 整包保存：所有注册的 KV 作为一个完整帧写入，原子提交
 *          - 磨损均衡：多扇区轮转写入，写满后 GC 搬迁最新帧并擦除旧扇区
 *          - 断电安全：帧头 CRC16 + 数据 CRC32 + commit_magic 三重保护
 *          - 颗粒度自适应：支持 8/32/64/128/256bit 写入颗粒度
 *          - 空间高效：固定开销仅 24B/帧，远低于 EasyFlash 的 48B/ENV
 *
 * @par 帧格式
 *          +--------+---------+------------+----------+--------------+
 *          | magic  | version | frame_len  | kv_count | header_crc16 |
 *          |  4B    |   4B    |    4B      |   2B     |     2B       |
 *          +--------+---------+------------+----------+--------------+
 *          |                  KV 数据区（变长）                       |
 *          |  [key_len(1)][key][val_len(2)][value] ...               |
 *          +----------------------------------------------------------+
 *          | data_crc32(4B) | commit_magic(4B)                       |
 *          +----------------------------------------------------------+
 *          总固定开销：16B(帧头) + 8B(帧尾) = 24B
 *
 * @par 使用示例
 * @code
 *          static uint8_t s_frame_buf[1024];
 *          static ring_storage_context_t s_storage;
 *
 *          const ring_storage_config_t cfg = {
 *              .start_addr        = 0x08078000,
 *              .area_size         = 8192,
 *              .sector_size       = 2048,
 *              .write_gran        = 64,
 *              .frame_buffer      = s_frame_buf,
 *              .frame_buffer_size = sizeof(s_frame_buf),
 *          };
 *
 *          ring_storage_init(&s_storage, &cfg);
 *          ring_storage_register(&s_storage, "motor_poles", &g_poles, sizeof(g_poles));
 *          ring_storage_register(&s_storage, "pid_kp", &g_kp, sizeof(g_kp));
 *
 *          ring_storage_load(&s_storage);  // 启动时加载
 *          // ... 修改参数后
 *          ring_storage_save(&s_storage);  // 持久化
 * @endcode
 */

#ifndef __RING_STORAGE_H
#define __RING_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/

/** 最大 KV 条目数 */
#define RING_STORAGE_MAX_KV                 32

/** 最大 key 长度（不含 '\0'） */
#define RING_STORAGE_KEY_MAX                31

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 环形存储错误码枚举
 */
typedef enum {
    RING_STORAGE_OK = 0,                    /**< 操作成功 */
    RING_STORAGE_ERROR_NULL_PTR,            /**< 空指针错误 */
    RING_STORAGE_ERROR_INVALID_PARAM,       /**< 无效参数 */
    RING_STORAGE_ERROR_UNINITIALIZED,       /**< 未初始化 */
    RING_STORAGE_ERROR_BUFFER_TOO_SMALL,    /**< 缓冲区过小 */
    RING_STORAGE_ERROR_KV_TABLE_FULL,       /**< KV 注册表已满 */
    RING_STORAGE_ERROR_KV_DUPLICATE,        /**< KV key 重复 */
    RING_STORAGE_ERROR_KEY_TOO_LONG,        /**< key 超过最大长度 */
    RING_STORAGE_ERROR_FLASH_READ,          /**< Flash 读取失败 */
    RING_STORAGE_ERROR_FLASH_WRITE,         /**< Flash 写入失败 */
    RING_STORAGE_ERROR_FLASH_ERASE,         /**< Flash 擦除失败 */
    RING_STORAGE_ERROR_NO_VALID_FRAME,      /**< 无有效帧（首次使用） */
    RING_STORAGE_ERROR_CRC,                 /**< CRC 校验失败 */
    RING_STORAGE_ERROR_GC_FAILED,           /**< GC 失败 */
} ring_storage_error_t;

/**
 * @brief 环形存储配置结构体
 */
typedef struct {
    uint32_t start_addr;                    /**< Flash 区域起始地址（须对齐到 sector_size） */
    uint32_t area_size;                     /**< Flash 区域总大小（须为 sector_size 的整数倍，>= 2 个扇区） */
    uint32_t sector_size;                   /**< 扇区大小（= 最小擦除单位，如 STM32G4 为 2048） */
    uint32_t write_gran;                    /**< 写入颗粒度（bit：8/32/64/128/256） */
    uint8_t* frame_buffer;                  /**< 帧序列化缓冲区（RAM），需 >= 最大帧大小 */
    uint16_t frame_buffer_size;             /**< 帧缓冲区大小 */
} ring_storage_config_t;

/**
 * @brief KV 注册条目
 */
typedef struct {
    const char* key;                        /**< KV 名称（以 '\0' 结尾，长度 <= RING_STORAGE_KEY_MAX） */
    void* value;                            /**< KV 值数据地址 */
    uint16_t value_len;                     /**< KV 值数据长度 */
} ring_storage_kv_entry_t;

/**
 * @brief 环形存储上下文结构体前向声明
 */
typedef struct ring_storage_context ring_storage_context_t;

/**
 * @brief 环形存储上下文结构体
 */
struct ring_storage_context {
    ring_storage_config_t config;           /**< 配置参数 */

    /* KV 注册表 */
    ring_storage_kv_entry_t kv_table[RING_STORAGE_MAX_KV]; /**< KV 条目数组 */
    uint8_t kv_count;                       /**< 已注册 KV 数量 */

    /* 运行时状态 */
    uint32_t active_sector_addr;            /**< 当前活动扇区起始地址 */
    uint32_t write_offset;                  /**< 活动扇区内写入偏移（相对于扇区起始） */
    uint32_t latest_version;                /**< 最新有效帧版本号 */
    uint32_t latest_frame_addr;             /**< 最新有效帧起始地址 */

    bool initialized;                       /**< 初始化标志 */
};

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief   初始化环形存储模块
 * @param   ctx     上下文指针
 * @param   config  配置结构体指针
 * @return  操作结果错误码
 * @note    初始化时自动扫描 Flash，定位最新有效帧。
 *          若无有效帧（首次使用），以空状态启动，首次 save 时创建第一帧。
 */
ring_storage_error_t ring_storage_init(ring_storage_context_t* ctx,
                                       const ring_storage_config_t* config);

/**
 * @brief   反初始化环形存储模块
 * @param   ctx 上下文指针
 */
void ring_storage_deinit(ring_storage_context_t* ctx);

/**
 * @brief   检查模块是否已初始化
 * @param   ctx 上下文指针
 * @return  true 已初始化，false 未初始化
 */
bool ring_storage_is_initialized(const ring_storage_context_t* ctx);

/**
 * @brief   注册一个 KV 条目
 * @param   ctx       上下文指针
 * @param   key       KV 名称（以 '\0' 结尾）
 * @param   value     KV 值数据地址
 * @param   value_len KV 值数据长度
 * @return  操作结果错误码
 * @note    必须在 save/load 之前注册所有 KV。
 *          key 不可重复，长度不超过 RING_STORAGE_KEY_MAX。
 */
ring_storage_error_t ring_storage_register(ring_storage_context_t* ctx,
                                           const char* key,
                                           void* value,
                                           uint16_t value_len);

/**
 * @brief   保存所有已注册的 KV 到 Flash
 * @param   ctx 上下文指针
 * @return  操作结果错误码
 * @note    原子操作：将所有 KV 序列化为一个完整帧写入 Flash。
 *          若活动扇区空间不足，自动触发 GC。
 *          断电安全：commit_magic 最后写入，未完成写入会被跳过。
 */
ring_storage_error_t ring_storage_save(ring_storage_context_t* ctx);

/**
 * @brief   从 Flash 加载所有已注册的 KV
 * @param   ctx 上下文指针
 * @return  操作结果错误码
 * @note    从最新有效帧加载所有 KV 到注册的 value 地址。
 *          若某个 key 在帧中不存在，保持 value 不变。
 *          首次使用（无有效帧）返回 RING_STORAGE_ERROR_NO_VALID_FRAME。
 */
ring_storage_error_t ring_storage_load(ring_storage_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* __RING_STORAGE_H */
