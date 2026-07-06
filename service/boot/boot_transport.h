/**
 * @file    boot_transport.h
 * @brief   CAN/CAN FD 引导协议帧编解码（纯数据变换，无状态）
 * @attention
 *
 * 本模块负责 boot_design.md §2.2 中定义的协议帧与 drv_can_msg_t 之间的转换。
 * 所有函数为纯数据变换，可重入，不维护任何内部状态。
 */

#ifndef __BOOT_TRANSPORT_H
#define __BOOT_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

#include "drv_can.h"

/* Exported constants --------------------------------------------------------*/

/** CAN ID 定义 */
#define BOOT_CAN_ID_HOST_TO_NODE  0x701U   /**< 上位机 → 板卡 */
#define BOOT_CAN_ID_NODE_TO_HOST  0x702U   /**< 板卡 → 上位机 */

/** 支持的离散帧物理长度集合（boot_design.md §2.2） */
#define BOOT_FRAME_SIZE_CLASSIC_CAN  8U     /**< 经典 CAN：8 字节 */
#define BOOT_FRAME_SIZES_COUNT       8U     /**< 离散长度数量 */

/** 协议控制开销（Command + Sequence） */
#define BOOT_FRAME_HEADER_LEN  2U           /**< Byte 0: Command, Byte 1: Sequence */

/** Block 大小 */
#define BOOT_BLOCK_SIZE  1024U              /**< 1KB 数据块 */

/** 帧类型枚举（boot_design.md §2.2.1） */
typedef enum {
    BOOT_CMD_START     = 0x01U, /**< 开始升级（上位机 → 板卡） */
    BOOT_CMD_METADATA  = 0x02U, /**< 元数据（上位机 → 板卡） */
    BOOT_CMD_DATA      = 0x03U, /**< 数据帧（上位机 → 板卡） */
    BOOT_CMD_VERIFY    = 0x04U, /**< 校验请求 */
    BOOT_CMD_REBOOT    = 0x05U, /**< 复位重启 */
    BOOT_CMD_DATA_END  = 0x08U, /**< 分块尾帧 */
    BOOT_CMD_ACK       = 0x10U, /**< 应答（板卡 → 上位机） */
    BOOT_CMD_NACK      = 0x11U, /**< 否定应答（板卡 → 上位机） */
} boot_cmd_t;

/** ACK/NACK 状态码 */
typedef enum {
    BOOT_STATUS_OK                = 0x00U, /**< 成功 */
    BOOT_STATUS_BLOCK_CHECKSUM    = 0x01U, /**< Block 累加和校验失败 */
    BOOT_STATUS_FLASH_WRITE_ERR   = 0x02U, /**< Flash 写入失败 */
    BOOT_STATUS_FLASH_VERIFY_ERR  = 0x03U, /**< Flash 读回校验失败 */
    BOOT_STATUS_CRC32_ERR         = 0x04U, /**< 整包 CRC32 校验失败 */
    BOOT_STATUS_INVALID_FRAME     = 0x05U, /**< 无效帧 */
    BOOT_STATUS_INVALID_STATE     = 0x06U, /**< 状态机不允许此命令 */
    BOOT_STATUS_TIMEOUT           = 0x07U, /**< 超时 */
    BOOT_STATUS_HW_MISMATCH       = 0x08U, /**< 硬件兼容 ID 不匹配 */
    BOOT_STATUS_FLASH_ERASE_ERR   = 0x09U, /**< Flash 擦除失败 */
    BOOT_STATUS_FLASH_READ_ERR    = 0x0AU, /**< Flash 读取失败 */
} boot_status_t;

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 获取支持的离散帧长度数组
 * @return 指向静态数组的指针
 */
const uint8_t* boot_transport_supported_frame_sizes(void);

/**
 * @brief 校验 Max_Frame_Size 是否在离散长度集合中
 * @param frame_size 待校验的帧长度
 * @return true 表示合法
 */
bool boot_transport_is_valid_frame_size(uint8_t frame_size);

/**
 * @brief 计算单帧数据载荷大小
 * @param max_frame_size 协商的单帧物理总长度
 * @return 数据载荷字节数（Max_Frame_Size - 2）
 */
static inline uint8_t boot_transport_payload_size(uint8_t max_frame_size)
{
    return max_frame_size - BOOT_FRAME_HEADER_LEN;
}

/**
 * @brief 从 CAN 消息提取帧头（Command + Sequence）
 * @param msg CAN 消息
 * @param cmd 输出：命令字
 * @param seq 输出：包序号
 */
void boot_transport_parse_header(const drv_can_msg_t* msg,
    uint8_t* cmd, uint8_t* seq);

/**
 * @brief 解析 START 帧
 * @param msg CAN 消息
 * @param fw_size 输出：固件总大小 (uint32, Byte 2-5)
 * @param hw_id 输出：硬件兼容 ID (uint16, Byte 6-7)
 * @param max_frame_size 输出：单帧物理长度 (uint8, Byte 8)
 * @return true 表示帧格式有效
 */
bool boot_transport_parse_start(const drv_can_msg_t* msg,
    uint32_t* fw_size, uint16_t* hw_id, uint8_t* max_frame_size);

/**
 * @brief 解析 METADATA 帧
 * @param msg CAN 消息
 * @param crc32 输出：整包 CRC32 (uint32, Byte 1-4)
 * @param version 输出：版本号 (uint16, Byte 5-6)
 * @return true 表示帧格式有效
 */
bool boot_transport_parse_metadata(const drv_can_msg_t* msg,
    uint32_t* crc32, uint16_t* version);

/**
 * @brief 解析 DATA 帧，提取数据载荷
 * @param msg CAN 消息
 * @param seq 输出：包序号
 * @param payload 输出：数据载荷起始指针（指向 msg->data 内部，非拷贝）
 * @param payload_len 输出：数据载荷长度
 */
void boot_transport_parse_data(const drv_can_msg_t* msg,
    uint8_t* seq, const uint8_t** payload, uint8_t* payload_len);

/**
 * @brief 解析 DATA_END 帧
 * @param msg CAN 消息
 * @param seq 输出：该分块最后一帧的序号
 * @param checksum 输出：16 位累加和校验码（从 Byte 2-3 固定位置读取）
 * @param remaining_data 输出：剩余数据起始指针（指向 msg->data 内部，非拷贝）
 * @param rem_len 输出：剩余数据长度
 */
void boot_transport_parse_data_end(const drv_can_msg_t* msg,
    uint8_t* seq, uint16_t* checksum,
    const uint8_t** remaining_data, uint8_t* rem_len);

/**
 * @brief 计算 1KB Block 的 16 位累加和校验
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return 16 位累加和
 */
uint16_t boot_transport_compute_block_checksum(const uint8_t* data, uint32_t len);

/**
 * @brief 构造 ACK 帧
 * @param msg 输出：CAN 消息
 * @param cmd 应答的命令字
 * @param status 状态码
 */
void boot_transport_build_ack(drv_can_msg_t* msg, uint8_t cmd, uint8_t status);

/**
 * @brief 构造 NACK 帧
 * @param msg 输出：CAN 消息
 * @param cmd 否定应答的命令字
 * @param error_code 错误码
 */
void boot_transport_build_nack(drv_can_msg_t* msg, uint8_t cmd, uint8_t error_code);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_TRANSPORT_H */
