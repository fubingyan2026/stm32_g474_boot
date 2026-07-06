/**
 * @file    boot_fsm.c
 * @brief   固件升级状态机实现
 */

/* Includes ------------------------------------------------------------------*/
#include "boot_fsm.h"

#include <string.h>

#include "boot_flash.h"
#include "boot_transport.h"
#include "fsm.h"
#include "log.h"

/* Private constants ---------------------------------------------------------*/

/** 上位机 TAG */
#define BOOT_TAG "boot_fsm"

/* Private variables ---------------------------------------------------------*/

/* 待处理消息（在 handler 中访问） */
static const drv_can_msg_t* s_pending_msg;

/* Private function prototypes -----------------------------------------------*/

static fsm_state_t handler_idle(fsm_t* fsm);
static fsm_state_t handler_start(fsm_t* fsm);
static fsm_state_t handler_data_transfer(fsm_t* fsm);
static fsm_state_t handler_verify_pending(fsm_t* fsm);
static fsm_state_t handler_reboot_pending(fsm_t* fsm);

/* 辅助函数 */
static void send_ack(boot_fsm_context_t* ctx, uint8_t cmd);
static void send_nack(boot_fsm_context_t* ctx, uint8_t cmd, uint8_t error);
static void reset_transfer_state(boot_fsm_context_t* ctx);
static bool validate_msg_id(const drv_can_msg_t* msg);

/* 计算一个 Block 的帧数 */
static inline uint8_t block_frame_count(uint8_t max_frame_size)
{
    uint8_t d = boot_transport_payload_size(max_frame_size);
    return (uint8_t)((BOOT_BLOCK_SIZE + d - 1U) / d);
}

/* Exported functions --------------------------------------------------------*/

bool boot_fsm_init(boot_fsm_context_t* ctx, void* fsm_instance,
    const boot_fsm_config_t* config)
{
    fsm_t* fsm = (fsm_t*)fsm_instance;
    fsm_config_t fsm_cfg;
    fsm_err_t err;

    /* 状态处理器表 */
    static fsm_handler_t s_handlers[BOOT_STATE_COUNT];
    /* 转换矩阵 */
    static fsm_guard_t s_transitions[BOOT_STATE_COUNT * BOOT_STATE_COUNT];

    if (!ctx || !fsm || !config) {
        return false;
    }
    if (!config->write_block_cb || !config->verify_block_cb
        || !config->verify_fw_cb || !config->erase_cb
        || !config->set_flag_cb || !config->reset_cb) {
        return false;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->config = *config;
    ctx->fsm = fsm;

    /* 设置状态处理器 */
    s_handlers[BOOT_STATE_IDLE]            = handler_idle;
    s_handlers[BOOT_STATE_START]           = handler_start;
    s_handlers[BOOT_STATE_DATA_TRANSFER]   = handler_data_transfer;
    s_handlers[BOOT_STATE_VERIFY_PENDING]  = handler_verify_pending;
    s_handlers[BOOT_STATE_REBOOT_PENDING]  = handler_reboot_pending;

    /* 组装 fsm 配置 */
    fsm_cfg.handlers = s_handlers;
    fsm_cfg.transitions = s_transitions;
    fsm_cfg.state_count = BOOT_STATE_COUNT;

    /* 初始化转换矩阵：全部禁止 */
    memset(s_transitions, 0, sizeof(s_transitions));

    /* 设置允许的转移（使用 fsm_always_true 哨兵，必须在 transitions 赋值后调用 fsm_at） */
    *fsm_at(&fsm_cfg, BOOT_STATE_IDLE, BOOT_STATE_START) = fsm_always_true;
    *fsm_at(&fsm_cfg, BOOT_STATE_START, BOOT_STATE_DATA_TRANSFER) = fsm_always_true;
    *fsm_at(&fsm_cfg, BOOT_STATE_START, BOOT_STATE_IDLE) = fsm_always_true;
    *fsm_at(&fsm_cfg, BOOT_STATE_DATA_TRANSFER, BOOT_STATE_VERIFY_PENDING) = fsm_always_true;
    *fsm_at(&fsm_cfg, BOOT_STATE_DATA_TRANSFER, BOOT_STATE_IDLE) = fsm_always_true;
    *fsm_at(&fsm_cfg, BOOT_STATE_VERIFY_PENDING, BOOT_STATE_REBOOT_PENDING) = fsm_always_true;
    *fsm_at(&fsm_cfg, BOOT_STATE_VERIFY_PENDING, BOOT_STATE_IDLE) = fsm_always_true;
    *fsm_at(&fsm_cfg, BOOT_STATE_REBOOT_PENDING, BOOT_STATE_IDLE) = fsm_always_true;
    fsm_cfg.entry_cb = NULL;
    fsm_cfg.exit_cb = NULL;
    fsm_cfg.state_names = NULL;
    fsm_cfg.user_data = ctx;

    err = fsm_init(fsm, BOOT_STATE_IDLE, &fsm_cfg);
    if (err != FSM_OK) {
        return false;
    }

    ctx->initialized = true;
    return true;
}

void boot_fsm_process_msg(boot_fsm_context_t* ctx, const drv_can_msg_t* msg)
{
    if (!ctx || !msg) {
        return;
    }

    /* 只处理来自上位机的消息 */
    if (!validate_msg_id(msg)) {
        return;
    }

    /* 刷新活动计时 */
    ctx->last_activity_tick = ctx->tick_count;

    /* 设置全局待处理消息指针，供 handler 访问 */
    s_pending_msg = msg;

    /* 驱动状态机 */
    fsm_step((fsm_t*)ctx->fsm);

    s_pending_msg = NULL;
}

void boot_fsm_tick(boot_fsm_context_t* ctx)
{
    uint32_t elapsed;
    if (!ctx) {
        return;
    }

    ctx->tick_count++;

    /* 检查超时：IDLE 状态不超时 */
    if (fsm_current_state((fsm_t*)ctx->fsm) == BOOT_STATE_IDLE) {
        ctx->last_activity_tick = ctx->tick_count;
        return;
    }

    elapsed = ctx->tick_count - ctx->last_activity_tick;
    if (elapsed > BOOT_FSM_TIMEOUT_TICKS) {
        LOG_W(BOOT_TAG, "超时: 10 秒无活动，复位到 IDLE");
        /* 超时：复位到 IDLE */
        fsm_goto((fsm_t*)ctx->fsm, BOOT_STATE_IDLE);
        ctx->last_activity_tick = ctx->tick_count;
    }
}

uint8_t boot_fsm_get_state(const boot_fsm_context_t* ctx)
{
    return ctx ? fsm_current_state((fsm_t*)ctx->fsm) : 0U;
}

bool boot_fsm_get_response(boot_fsm_context_t* ctx, drv_can_msg_t* msg)
{
    if (!ctx || !msg || !ctx->has_response) {
        return false;
    }
    *msg = ctx->response_msg;
    ctx->has_response = false;
    return true;
}

/* Private functions ---------------------------------------------------------*/

static bool validate_msg_id(const drv_can_msg_t* msg)
{
    return msg->id == BOOT_CAN_ID_HOST_TO_NODE && !msg->is_extended;
}

static void send_ack(boot_fsm_context_t* ctx, uint8_t cmd)
{
    boot_transport_build_ack(&ctx->response_msg, cmd, BOOT_STATUS_OK);
    ctx->has_response = true;
}

static void send_nack(boot_fsm_context_t* ctx, uint8_t cmd, uint8_t error)
{
    boot_transport_build_nack(&ctx->response_msg, cmd, error);
    ctx->has_response = true;
}

static void reset_transfer_state(boot_fsm_context_t* ctx)
{
    ctx->block_accumulated_len = 0U;
    ctx->total_received = 0U;
    ctx->expected_seq = 0U;
    memset(ctx->ram_block_buffer, 0, sizeof(ctx->ram_block_buffer));
}

/* ===== 状态处理器 =========================================================== */

/**
 * @brief IDLE 状态处理器：等待 START 帧
 */
static fsm_state_t handler_idle(fsm_t* fsm)
{
    boot_fsm_context_t* ctx = (boot_fsm_context_t*)fsm_user_data(fsm);
    uint8_t cmd, seq;
    uint32_t fw_size;
    uint16_t hw_id;
    uint8_t max_frame_size;

    boot_transport_parse_header(s_pending_msg, &cmd, &seq);

    if (cmd != BOOT_CMD_START) {
        /* IDLE 状态下只接受 START */
        return BOOT_STATE_IDLE;
    }

    LOG_I(BOOT_TAG, "收到 START 帧");

    if (!boot_transport_parse_start(s_pending_msg, &fw_size, &hw_id, &max_frame_size)) {
        LOG_E(BOOT_TAG, "START 帧格式无效，拒绝");
        send_nack(ctx, BOOT_CMD_START, BOOT_STATUS_INVALID_FRAME);
        return BOOT_STATE_IDLE;
    }

    LOG_I(BOOT_TAG, "START: fw_size=%u, hw_id=0x%04X, max_frame_size=%u",
        fw_size, hw_id, max_frame_size);

    /* 校验硬件兼容 ID */
    if (hw_id != ctx->config.hw_compat_id) {
        LOG_E(BOOT_TAG, "硬件 ID 不匹配: 期望 0x%04X, 收到 0x%04X",
            ctx->config.hw_compat_id, hw_id);
        send_nack(ctx, BOOT_CMD_START, BOOT_STATUS_HW_MISMATCH);
        return BOOT_STATE_IDLE;
    }

    /* 校验固件大小不超过分区 */
    if (fw_size == 0U || fw_size > BOOT_FLASH_APP_SIZE) {
        LOG_E(BOOT_TAG, "固件大小越界: %u (上限 %u)", fw_size, BOOT_FLASH_APP_SIZE);
        send_nack(ctx, BOOT_CMD_START, BOOT_STATUS_INVALID_FRAME);
        return BOOT_STATE_IDLE;
    }

    /* 保存协商参数 */
    ctx->fw_total_size = fw_size;
    ctx->max_frame_size = max_frame_size;
    reset_transfer_state(ctx);

    /* 擦除目标分区（默认写 App A） */
    ctx->target_partition = BOOT_PARTITION_A;
    LOG_I(BOOT_TAG, "擦除目标分区 App %c", 'A' + ctx->target_partition);
    if (ctx->config.erase_cb(ctx->config.user_data) != 0U) {
        LOG_E(BOOT_TAG, "分区擦除失败");
        send_nack(ctx, BOOT_CMD_START, BOOT_STATUS_FLASH_ERASE_ERR);
        return BOOT_STATE_IDLE;
    }

    LOG_I(BOOT_TAG, "握手完成 → START 状态");
    send_ack(ctx, BOOT_CMD_START);
    return BOOT_STATE_START;
}

/**
 * @brief START 状态处理器：等待 METADATA
 */
static fsm_state_t handler_start(fsm_t* fsm)
{
    boot_fsm_context_t* ctx = (boot_fsm_context_t*)fsm_user_data(fsm);
    uint8_t cmd, seq;
    uint32_t crc32;
    uint16_t version;

    boot_transport_parse_header(s_pending_msg, &cmd, &seq);

    /* 允许重复 START（重新协商） */
    if (cmd == BOOT_CMD_START) {
        uint32_t fw_size;
        uint16_t hw_id;
        uint8_t max_frame_size;
        if (boot_transport_parse_start(s_pending_msg, &fw_size, &hw_id, &max_frame_size)
            && hw_id == ctx->config.hw_compat_id && fw_size > 0U
            && fw_size <= BOOT_FLASH_APP_SIZE) {
            LOG_W(BOOT_TAG, "收到重复 START，重新协商参数");
            ctx->fw_total_size = fw_size;
            ctx->max_frame_size = max_frame_size;
            reset_transfer_state(ctx);
            send_ack(ctx, BOOT_CMD_START);
        }
        return BOOT_STATE_START;
    }

    if (cmd != BOOT_CMD_METADATA) {
        LOG_W(BOOT_TAG, "收到非预期命令 0x%02X，期望 METADATA(0x02)", cmd);
        send_nack(ctx, cmd, BOOT_STATUS_INVALID_STATE);
        return BOOT_STATE_START;
    }

    if (!boot_transport_parse_metadata(s_pending_msg, &crc32, &version)) {
        LOG_E(BOOT_TAG, "METADATA 帧格式无效");
        send_nack(ctx, BOOT_CMD_METADATA, BOOT_STATUS_INVALID_FRAME);
        return BOOT_STATE_START;
    }

    /* 保存元数据 */
    ctx->fw_crc32 = crc32;
    ctx->fw_version = version;
    LOG_I(BOOT_TAG, "收到 METADATA: crc32=0x%08X, version=%u", crc32, version);

    send_ack(ctx, BOOT_CMD_METADATA);
    LOG_I(BOOT_TAG, "→ DATA_TRANSFER 状态");
    return BOOT_STATE_DATA_TRANSFER;
}

/**
 * @brief DATA_TRANSFER 状态处理器：接收数据帧
 */
static fsm_state_t handler_data_transfer(fsm_t* fsm)
{
    boot_fsm_context_t* ctx = (boot_fsm_context_t*)fsm_user_data(fsm);
    uint8_t cmd, seq;
    uint8_t payload_len;
    const uint8_t* payload;
    uint8_t d;

    boot_transport_parse_header(s_pending_msg, &cmd, &seq);

    if (cmd == BOOT_CMD_DATA) {
        boot_transport_parse_data(s_pending_msg, &seq, &payload, &payload_len);
        d = boot_transport_payload_size(ctx->max_frame_size);

        /* 序号检查 */
        if (seq != ctx->expected_seq) {
            LOG_W(BOOT_TAG, "DATA 序号错乱: 期望 %u, 收到 %u", ctx->expected_seq, seq);
            send_nack(ctx, BOOT_CMD_DATA, BOOT_STATUS_INVALID_FRAME);
            return BOOT_STATE_DATA_TRANSFER;
        }

        /* 累积数据到 1KB 缓冲区 */
        if (ctx->block_accumulated_len + payload_len <= BOOT_BLOCK_SIZE) {
            memcpy(&ctx->ram_block_buffer[ctx->block_accumulated_len],
                payload, payload_len);
            ctx->block_accumulated_len = (uint16_t)(ctx->block_accumulated_len + payload_len);
        }
        ctx->expected_seq++;

        LOG_D(BOOT_TAG, "DATA seq=%u, len=%u, accumulated=%u",
            seq, payload_len, ctx->block_accumulated_len);

        /* 检查是否满 1KB（非尾帧情况不应满，但做安全检查） */
        if (ctx->block_accumulated_len >= BOOT_BLOCK_SIZE) {
            /* 不应在 DATA 帧就满，必须等 DATA_END */
            LOG_W(BOOT_TAG, "BUFFER 已满但未收到 DATA_END，复位");
            send_nack(ctx, BOOT_CMD_DATA, BOOT_STATUS_INVALID_FRAME);
            ctx->block_accumulated_len = 0U;
            return BOOT_STATE_DATA_TRANSFER;
        }

        return BOOT_STATE_DATA_TRANSFER;
    }

    if (cmd == BOOT_CMD_DATA_END) {
        uint16_t expected_checksum, calculated_checksum;
        const uint8_t* remaining_data;
        uint8_t rem_len;

        boot_transport_parse_data_end(s_pending_msg, &seq,
            &expected_checksum, &remaining_data, &rem_len);

        /* 累积剩余数据 */
        if (rem_len > 0U) {
            if (ctx->block_accumulated_len + rem_len <= BOOT_BLOCK_SIZE) {
                memcpy(&ctx->ram_block_buffer[ctx->block_accumulated_len],
                    remaining_data, rem_len);
                ctx->block_accumulated_len = (uint16_t)(ctx->block_accumulated_len + rem_len);
            }
        }

        LOG_D(BOOT_TAG, "DATA_END: seq=%u, rem_len=%u, expected_cs=0x%04X",
            seq, rem_len, expected_checksum);

        /* 计算 1KB Block 的累加和 */
        calculated_checksum = boot_transport_compute_block_checksum(
            ctx->ram_block_buffer, BOOT_BLOCK_SIZE);

        if (calculated_checksum != expected_checksum) {
            LOG_E(BOOT_TAG, "Block checksum 失败: 期望 0x%04X, 计算 0x%04X",
                expected_checksum, calculated_checksum);
            send_nack(ctx, BOOT_CMD_DATA_END, BOOT_STATUS_BLOCK_CHECKSUM);
            /* 复位当前 Block，等待重发 */
            ctx->block_accumulated_len = 0U;
            return BOOT_STATE_DATA_TRANSFER;
        }

        LOG_I(BOOT_TAG, "Block checksum OK (0x%04X)，写入 Flash offset=%u",
            calculated_checksum, ctx->total_received);

        /* Block 校验通过：写入 Flash */
        if (ctx->config.write_block_cb(ctx->config.user_data,
                ctx->total_received, ctx->ram_block_buffer,
                BOOT_BLOCK_SIZE) != 0U) {
            LOG_E(BOOT_TAG, "Flash 写入失败, offset=%u", ctx->total_received);
            send_nack(ctx, BOOT_CMD_DATA_END, BOOT_STATUS_FLASH_WRITE_ERR);
            return BOOT_STATE_IDLE;
        }

        /* Flash 读回校验 */
        if (ctx->config.verify_block_cb(ctx->config.user_data,
                ctx->total_received, ctx->ram_block_buffer,
                BOOT_BLOCK_SIZE) != 0U) {
            LOG_E(BOOT_TAG, "Flash 读回校验失败, offset=%u", ctx->total_received);
            send_nack(ctx, BOOT_CMD_DATA_END, BOOT_STATUS_FLASH_VERIFY_ERR);
            return BOOT_STATE_IDLE;
        }

        ctx->total_received += BOOT_BLOCK_SIZE;
        ctx->block_accumulated_len = 0U;
        ctx->expected_seq = 0U;
        memset(ctx->ram_block_buffer, 0, sizeof(ctx->ram_block_buffer));

        send_ack(ctx, BOOT_CMD_DATA_END);

        /* 检查是否全部接收完毕 */
        if (ctx->total_received >= ctx->fw_total_size) {
            LOG_I(BOOT_TAG, "全部数据接收完毕 (%u 字节)，等待 VERIFY", ctx->fw_total_size);
            return BOOT_STATE_VERIFY_PENDING;
        }

        LOG_I(BOOT_TAG, "Block 完成, 已接收 %u/%u 字节",
            ctx->total_received, ctx->fw_total_size);
        return BOOT_STATE_DATA_TRANSFER;
    }

    /* 其他命令：非法 */
    LOG_W(BOOT_TAG, "DATA_TRANSFER 状态收到非预期命令 0x%02X", cmd);
    send_nack(ctx, cmd, BOOT_STATUS_INVALID_STATE);
    return BOOT_STATE_DATA_TRANSFER;
}

/**
 * @brief VERIFY_PENDING 状态处理器：等待 VERIFY 指令
 */
static fsm_state_t handler_verify_pending(fsm_t* fsm)
{
    boot_fsm_context_t* ctx = (boot_fsm_context_t*)fsm_user_data(fsm);
    uint8_t cmd, seq;
    uint32_t calculated_crc32;

    boot_transport_parse_header(s_pending_msg, &cmd, &seq);

    /* 允许重复 VERIFY */
    if (cmd != BOOT_CMD_VERIFY) {
        LOG_W(BOOT_TAG, "VERIFY_PENDING 收到非预期命令 0x%02X", cmd);
        send_nack(ctx, cmd, BOOT_STATUS_INVALID_STATE);
        return BOOT_STATE_VERIFY_PENDING;
    }

    LOG_I(BOOT_TAG, "收到 VERIFY 命令，开始整包 CRC32 校验");

    /* 计算整包 CRC32 */
    if (ctx->config.verify_fw_cb(ctx->config.user_data,
            ctx->fw_total_size, &calculated_crc32) != 0U) {
        LOG_E(BOOT_TAG, "Flash 读取失败，无法计算 CRC32");
        send_nack(ctx, BOOT_CMD_VERIFY, BOOT_STATUS_FLASH_READ_ERR);
        return BOOT_STATE_IDLE;
    }

    if (calculated_crc32 != ctx->fw_crc32) {
        LOG_E(BOOT_TAG, "CRC32 不匹配: 期望 0x%08X, 计算 0x%08X",
            ctx->fw_crc32, calculated_crc32);
        send_nack(ctx, BOOT_CMD_VERIFY, BOOT_STATUS_CRC32_ERR);
        return BOOT_STATE_IDLE;
    }

    LOG_I(BOOT_TAG, "CRC32 校验通过 (0x%08X) → REBOOT_PENDING", calculated_crc32);
    send_ack(ctx, BOOT_CMD_VERIFY);
    return BOOT_STATE_REBOOT_PENDING;
}

/**
 * @brief REBOOT_PENDING 状态处理器：等待 REBOOT 指令
 */
static fsm_state_t handler_reboot_pending(fsm_t* fsm)
{
    boot_fsm_context_t* ctx = (boot_fsm_context_t*)fsm_user_data(fsm);
    uint8_t cmd, seq;

    boot_transport_parse_header(s_pending_msg, &cmd, &seq);

    if (cmd != BOOT_CMD_REBOOT) {
        LOG_W(BOOT_TAG, "REBOOT_PENDING 收到非预期命令 0x%02X", cmd);
        send_nack(ctx, cmd, BOOT_STATUS_INVALID_STATE);
        return BOOT_STATE_REBOOT_PENDING;
    }

    LOG_I(BOOT_TAG, "收到 REBOOT 命令，写入 Metadata 引导分区 %c",
        'A' + ctx->target_partition);

    /* 设置 Metadata 启动标志 */
    ctx->config.set_flag_cb(ctx->config.user_data,
        ctx->target_partition, ctx->fw_version,
        ctx->fw_total_size, ctx->fw_crc32);

    send_ack(ctx, BOOT_CMD_REBOOT);
    LOG_I(BOOT_TAG, "ACK 已发送，即将系统复位...");

    /* 软件复位 */
    ctx->config.reset_cb(ctx->config.user_data);

    /* 不应到达此处 */
    return BOOT_STATE_IDLE;
}
