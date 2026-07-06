/**
 * @file    boot_transport.c
 * @brief   CAN/CAN FD 引导协议帧编解码实现
 */

/* Includes ------------------------------------------------------------------*/
#include "boot_transport.h"

#include <string.h>

/* Private constants ---------------------------------------------------------*/

/** 支持的 CAN FD 离散长度（boot_design.md §2.2） */
static const uint8_t s_supported_frame_sizes[] = {
    8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U
};

/* Exported functions --------------------------------------------------------*/

const uint8_t* boot_transport_supported_frame_sizes(void)
{
    return s_supported_frame_sizes;
}

bool boot_transport_is_valid_frame_size(uint8_t frame_size)
{
    for (uint8_t i = 0U; i < BOOT_FRAME_SIZES_COUNT; i++) {
        if (s_supported_frame_sizes[i] == frame_size) {
            return true;
        }
    }
    return false;
}

void boot_transport_parse_header(const drv_can_msg_t* msg,
    uint8_t* cmd, uint8_t* seq)
{
    *cmd = msg->data[0];
    *seq = msg->data[1];
}

bool boot_transport_parse_start(const drv_can_msg_t* msg,
    uint32_t* fw_size, uint16_t* hw_id, uint8_t* max_frame_size)
{
    /* START 帧最少需要 9 字节：cmd(1) + seq(1) + fw_size(4) + hw_id(2) + frame_size(1) */
    if (msg->dlc < 9U) {
        return false;
    }

    *fw_size = ((uint32_t)msg->data[2] << 24)
             | ((uint32_t)msg->data[3] << 16)
             | ((uint32_t)msg->data[4] << 8)
             |  (uint32_t)msg->data[5];
    *hw_id   = ((uint16_t)msg->data[6] << 8) | (uint16_t)msg->data[7];
    *max_frame_size = msg->data[8];

    return boot_transport_is_valid_frame_size(*max_frame_size);
}

bool boot_transport_parse_metadata(const drv_can_msg_t* msg,
    uint32_t* crc32, uint16_t* version)
{
    /* METADATA 帧最少需要 7 字节：cmd(1) + seq(1) + crc32(4) + version(2) */
    if (msg->dlc < 7U) {
        return false;
    }

    *crc32   = ((uint32_t)msg->data[1] << 24)
             | ((uint32_t)msg->data[2] << 16)
             | ((uint32_t)msg->data[3] << 8)
             |  (uint32_t)msg->data[4];
    *version = ((uint16_t)msg->data[5] << 8) | (uint16_t)msg->data[6];
    return true;
}

void boot_transport_parse_data(const drv_can_msg_t* msg,
    uint8_t* seq, const uint8_t** payload, uint8_t* payload_len)
{
    *seq = msg->data[1];
    *payload_len = (uint8_t)(msg->dlc - BOOT_FRAME_HEADER_LEN);
    *payload = &msg->data[BOOT_FRAME_HEADER_LEN];
}

void boot_transport_parse_data_end(const drv_can_msg_t* msg,
    uint8_t* seq, uint16_t* checksum,
    const uint8_t** remaining_data, uint8_t* rem_len)
{
    *seq = msg->data[1];
    /* Checksum 固定在 Byte 2-3（boot_design.md §3.2） */
    *checksum = ((uint16_t)msg->data[2] << 8) | (uint16_t)msg->data[3];
    /* 剩余数据从 Byte 4 开始 */
    *rem_len = (uint8_t)(msg->dlc - 4U);
    *remaining_data = &msg->data[4];
}

uint16_t boot_transport_compute_block_checksum(const uint8_t* data, uint32_t len)
{
    uint16_t sum = 0U;
    for (uint32_t i = 0U; i < len; i++) {
        sum = (uint16_t)(sum + (uint16_t)data[i]);
    }
    return sum;
}

void boot_transport_build_ack(drv_can_msg_t* msg, uint8_t cmd, uint8_t status)
{
    memset(msg, 0, sizeof(*msg));
    msg->id = BOOT_CAN_ID_NODE_TO_HOST;
    msg->is_extended = false;
    msg->data[0] = cmd;
    msg->data[1] = status;
    /* ACK 帧使用经典 CAN 8 字节长度，其余填充 0 */
    msg->dlc = 8U;
}

void boot_transport_build_nack(drv_can_msg_t* msg, uint8_t cmd, uint8_t error_code)
{
    memset(msg, 0, sizeof(*msg));
    msg->id = BOOT_CAN_ID_NODE_TO_HOST;
    msg->is_extended = false;
    msg->data[0] = BOOT_CMD_NACK;
    msg->data[1] = cmd;        /* 对哪个命令的否定应答 */
    msg->data[2] = error_code; /* 错误码 */
    msg->dlc = 8U;
}
