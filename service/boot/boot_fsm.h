/**
 * @file    boot_fsm.h
 * @brief   固件升级状态机 — 使用 fsm 库实现协议状态转移
 * @attention
 *
 * 使用 m_middlewares/framework/fsm.h 扁平状态机框架。
 * 状态转移由守卫矩阵控制，非法转移在框架层拦截。
 */

#ifndef __BOOT_FSM_H
#define __BOOT_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

#include "drv_can.h"

/* Exported types ------------------------------------------------------------*/

/** 升级状态枚举 */
typedef enum {
    BOOT_STATE_IDLE = 0,           /**< 空闲，等待 START */
    BOOT_STATE_START,              /**< 已收到 START，等待 METADATA */
    BOOT_STATE_DATA_TRANSFER,      /**< 数据传输中 */
    BOOT_STATE_VERIFY_PENDING,     /**< 等待 VERIFY 指令 */
    BOOT_STATE_REBOOT_PENDING,     /**< 等待 REBOOT 指令 */
    BOOT_STATE_COUNT               /**< 状态总数 */
} boot_state_t;

/** 状态机回调：写 1KB Block 到 Flash */
typedef uint8_t (*boot_fsm_write_block_cb_t)(void* user_data,
    uint32_t offset, const uint8_t* data, uint32_t len);

/** 状态机回调：读回校验 1KB Block */
typedef uint8_t (*boot_fsm_verify_block_cb_t)(void* user_data,
    uint32_t offset, const uint8_t* data, uint32_t len);

/** 状态机回调：整包 CRC32 校验 */
typedef uint8_t (*boot_fsm_verify_fw_cb_t)(void* user_data,
    uint32_t size, uint32_t* checksum);

/** 状态机回调：擦除分区 */
typedef uint8_t (*boot_fsm_erase_cb_t)(void* user_data);

/** 状态机回调：写入 Metadata 启动标志 */
typedef uint8_t (*boot_fsm_set_flag_cb_t)(void* user_data,
    uint8_t boot_partition, uint16_t version,
    uint32_t fw_size, uint32_t fw_checksum);

/** 状态机回调：系统复位 */
typedef void (*boot_fsm_reset_cb_t)(void* user_data);

/* Exported macro ------------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/** 超时定义（ms） */
#define BOOT_FSM_TIMEOUT_MS  3000U  /**< 3000 毫秒超时 */

/* Exported functions prototypes ---------------------------------------------*/

/* 前向声明 */
typedef struct boot_fsm_context boot_fsm_context_t;

/** 配置结构体 */
typedef struct {
    /* 回调函数 */
    boot_fsm_write_block_cb_t write_block_cb;       /**< 写 Block 回调 */
    boot_fsm_verify_block_cb_t verify_block_cb;     /**< 校验 Block 回调 */
    boot_fsm_verify_fw_cb_t verify_fw_cb;           /**< 整包 CRC32 校验回调 */
    boot_fsm_erase_cb_t erase_cb;                   /**< 擦除分区回调 */
    boot_fsm_set_flag_cb_t set_flag_cb;             /**< 设置启动标志回调 */
    boot_fsm_reset_cb_t reset_cb;                   /**< 系统复位回调 */

    /* 用户自定义数据（透传给回调） */
    void* user_data;                                 /**< 用户数据指针 */

    /* 本地硬件 ID */
    uint16_t hw_compat_id;                           /**< 本机硬件兼容 ID */

    /* 目标分区 */
    uint8_t  target_partition;                       /**< 目标写入分区 (BOOT_PARTITION_A 或 B) */
} boot_fsm_config_t;

/** 状态机上下文（嵌套 fsm） */
struct boot_fsm_context {
    void* fsm;                     /**< fsm_t* 状态机实例（由调用者分配） */
    boot_fsm_config_t config;      /**< 配置参数 */

    /* 协商参数 */
    uint32_t fw_total_size;        /**< 固件总大小 */
    uint32_t fw_checksum;          /**< 固件校验和 */
    uint16_t fw_version;           /**< 固件版本号 */
    uint8_t  max_frame_size;       /**< 协商的单帧物理长度 */

    /* 传输状态 */
    uint8_t  ram_block_buffer[1024]; /**< 1KB 累积缓冲区 */
    uint16_t block_accumulated_len; /**< 当前 Block 已累积字节数 */
    uint32_t total_received;        /**< 已接收总字节数 */
    uint8_t  expected_seq;          /**< 期望的下一个 DATA 帧序号 */

    /* 超时管理 */
    uint32_t last_activity_tick;    /**< 最后活动的 tick 值 */
    uint32_t tick_count;            /**< 当前 tick 计数 */

    /* 响应消息 */
    drv_can_msg_t response_msg;    /**< 待发送的 ACK/NACK 消息 */
    bool has_response;             /**< 是否有待发送的响应 */

    /* 目标分区（START 阶段确定） */
    uint8_t  target_partition;     /**< 目标写入分区（A 或 B） */

    bool initialized;              /**< 初始化标志 */
};

/**
 * @brief 初始化升级状态机
 * @param ctx 状态机上下文指针
 * @param fsm_instance fsm_t 实例指针（由调用者静态分配）
 * @param config 配置结构体指针
 * @return true 表示成功
 */
bool boot_fsm_init(boot_fsm_context_t* ctx, void* fsm_instance,
    const boot_fsm_config_t* config);

/**
 * @brief 喂入一条 CAN 消息，驱动状态转移
 * @param ctx 状态机上下文指针
 * @param msg CAN 消息指针
 */
void boot_fsm_process_msg(boot_fsm_context_t* ctx, const drv_can_msg_t* msg);

/**
 * @brief 周期性调用，检查超时
 * @param ctx 状态机上下文指针
 */
void boot_fsm_tick(boot_fsm_context_t* ctx);

/**
 * @brief 查询当前状态
 * @param ctx 状态机上下文指针
 * @return 当前状态
 */
uint8_t boot_fsm_get_state(const boot_fsm_context_t* ctx);

/**
 * @brief 获取待发送的响应消息
 * @param ctx 状态机上下文指针
 * @param msg 输出：响应消息
 * @return true 表示有响应待发送
 */
bool boot_fsm_get_response(boot_fsm_context_t* ctx, drv_can_msg_t* msg);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_FSM_H */
