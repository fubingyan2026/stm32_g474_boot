/**
 * @file    boot_task.c
 * @brief   Bootloader 主任务实现
 */

/* Includes ------------------------------------------------------------------*/
#include "boot_task.h"

#include <string.h>

#include "app_main.h"
#include "boot_flash.h"
#include "boot_fsm.h"
#include "boot_transport.h"
#include "drv_can.h"
#include "drv_stm32g4_flash.h"
#include "fdcan.h"
#include "fsm.h"
#include "log.h"
#include "msg_fifo.h"
#include "sw_timer.h"

/* Private constants ---------------------------------------------------------*/

/** CAN RX 消息队列容量 */
#define BOOT_MSG_FIFO_SIZE  16U

/** 主循环轮询周期 (ms) */
#define BOOT_TASK_PERIOD_MS 1U

/* Private variables ---------------------------------------------------------*/

/* CAN 消息队列 */
static drv_can_msg_t s_msg_fifo_buffer[BOOT_MSG_FIFO_SIZE];
static msg_fifo_t s_msg_fifo;

/* Flash 分区管理器 */
static boot_flash_context_t s_flash_ctx;

/* 状态机实例 */
static fsm_t s_fsm;
static boot_fsm_context_t s_fsm_ctx;

/* 定时器 */
static sw_timer_t s_timer;

/* 目标分区 */
static boot_partition_t s_target_partition = BOOT_PARTITION_A;

/* Private function prototypes -----------------------------------------------*/

static void can_rx_callback(drv_can_channel_t ch, const drv_can_msg_t* msg);
static void boot_timer_cb(void* user_data);

/* 回调函数 */
static uint8_t write_block_cb(void* user_data, uint32_t offset,
    const uint8_t* data, uint32_t len);
static uint8_t verify_block_cb(void* user_data, uint32_t offset,
    const uint8_t* data, uint32_t len);
static uint8_t verify_fw_cb(void* user_data, uint32_t size, uint32_t* crc32);
static uint8_t erase_cb(void* user_data);
static uint8_t set_flag_cb(void* user_data, uint8_t boot_partition,
    uint16_t version, uint32_t fw_size, uint32_t fw_crc32);
static void reset_cb(void* user_data);

/* Exported functions --------------------------------------------------------*/

bool boot_task_try_boot_app(void)
{
    boot_flash_context_t flash_ctx;
    boot_metadata_t meta;

    boot_flash_init(&flash_ctx);
    boot_flash_read_metadata(&flash_ctx, &meta);

    /* 检查 Metadata 有效性 */
    if (meta.magic != BOOT_METADATA_MAGIC) {
        LOG_I("boot", "no valid metadata, enter bootloader mode");
        return false;
    }

    if (meta.upgrade_flag != 0U) {
        LOG_I("boot", "upgrade flag set, enter bootloader mode");
        return false;
    }

    /* CRC32 校验 App 分区 */
    uint32_t calculated_crc;
    boot_partition_t part = (meta.boot_partition == BOOT_PARTITION_A)
        ? BOOT_PARTITION_A : BOOT_PARTITION_B;
    if (boot_flash_compute_crc32(&flash_ctx, part,
            meta.fw_size, &calculated_crc) != BOOT_FLASH_OK) {
        LOG_E("boot", "CRC32 compute failed, enter bootloader");
        return false;
    }

    if (calculated_crc != meta.fw_crc32) {
        LOG_E("boot", "App CRC32 mismatch (calc=0x%08lx, expected=0x%08lx)",
            calculated_crc, meta.fw_crc32);
        return false;
    }

    /* 跳转到 App */
    LOG_I("boot", "booting app from partition %c, version %d",
        (part == BOOT_PARTITION_A) ? 'A' : 'B', meta.version);

    uint32_t app_addr = boot_flash_partition_addr(part);
    uint32_t app_sp = *(volatile uint32_t*)app_addr;
    uint32_t app_pc = *(volatile uint32_t*)(app_addr + 4U);

    /* 设置 MSP 并跳转 */
    __set_MSP(app_sp);
    ((void (*)(void))app_pc)();

    /* 不应到达此处 */
    return false;
}

void boot_task_init(void)
{
    boot_flash_error_t flash_err;
    boot_fsm_config_t fsm_config;
    drv_can_error_t can_err;

    LOG_I("boot", "initializing bootloader...");

    /* 初始化消息队列 */
    msg_fifo_init(&s_msg_fifo, s_msg_fifo_buffer,
        sizeof(s_msg_fifo_buffer), sizeof(drv_can_msg_t));

    /* 2. 初始化 Flash */
    flash_err = boot_flash_init(&s_flash_ctx);
    if (flash_err != BOOT_FLASH_OK) {
        LOG_E("boot", "flash init failed: %d", flash_err);
        return;
    }

    /* 初始化 CAN（通道 1） */
    can_err = drv_can_init(DRV_CAN_CH_1, &hfdcan1);
    if (can_err != DRV_CAN_OK) {
        LOG_E("boot", "CAN init failed: %d", can_err);
        return;
    }
    drv_can_register_rx_callback(DRV_CAN_CH_1, can_rx_callback);

    /* 初始化状态机 */
    memset(&fsm_config, 0, sizeof(fsm_config));
    fsm_config.write_block_cb = write_block_cb;
    fsm_config.verify_block_cb = verify_block_cb;
    fsm_config.verify_fw_cb = verify_fw_cb;
    fsm_config.erase_cb = erase_cb;
    fsm_config.set_flag_cb = set_flag_cb;
    fsm_config.reset_cb = reset_cb;
    fsm_config.user_data = NULL;
    fsm_config.hw_compat_id = 0x0001U; /* 硬件兼容 ID，按需修改 */

    if (!boot_fsm_init(&s_fsm_ctx, &s_fsm, &fsm_config)) {
        LOG_E("boot", "FSM init failed");
        return;
    }

    /* 创建轮询定时器 */
    sw_timer_init(&s_timer,
        &(sw_timer_config_t){
            .priority = SW_TIMER_PRIO_NORMAL,
            .callback = boot_timer_cb,
            .user_data = NULL
        });
    sw_timer_start(&s_timer, BOOT_TASK_PERIOD_MS, 0); /* 0 = 无限重复 */

    LOG_I("boot", "bootloader ready, waiting for CAN upgrade...");
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief CAN RX 中断回调（ISR 上下文）
 */
static void can_rx_callback(drv_can_channel_t ch, const drv_can_msg_t* msg)
{
    (void)ch;
    /* 仅处理来自上位机的消息 */
    if (msg->id == BOOT_CAN_ID_HOST_TO_NODE) {
        msg_fifo_push(&s_msg_fifo, msg);
    }
}

/**
 * @brief 主循环定时器回调（主循环上下文，5ms 周期）
 */
static void boot_timer_cb(void* user_data)
{
    drv_can_msg_t msg;
    drv_can_msg_t response;
    (void)user_data;

    /* 消费所有待处理 CAN 消息 */
    while (msg_fifo_pop(&s_msg_fifo, &msg)) {
        boot_fsm_process_msg(&s_fsm_ctx, &msg);

        /* 发送 ACK/NACK 响应 */
        if (boot_fsm_get_response(&s_fsm_ctx, &response)) {
            drv_can_send(DRV_CAN_CH_1, &response);
        }
    }

    /* 更新超时 */
    boot_fsm_tick(&s_fsm_ctx);
}

/* ===== 回调函数实现 ======================================================= */

static uint8_t write_block_cb(void* user_data, uint32_t offset,
    const uint8_t* data, uint32_t len)
{
    (void)user_data;
    boot_flash_error_t err = boot_flash_write_block(&s_flash_ctx,
        s_target_partition, offset, data, len);
    return (uint8_t)err;
}

static uint8_t verify_block_cb(void* user_data, uint32_t offset,
    const uint8_t* data, uint32_t len)
{
    (void)user_data;
    boot_flash_error_t err = boot_flash_verify_block(&s_flash_ctx,
        s_target_partition, offset, data, len);
    return (uint8_t)err;
}

static uint8_t verify_fw_cb(void* user_data, uint32_t size, uint32_t* crc32)
{
    (void)user_data;
    boot_flash_error_t err = boot_flash_compute_crc32(&s_flash_ctx,
        s_target_partition, size, crc32);
    return (uint8_t)err;
}

static uint8_t erase_cb(void* user_data)
{
    (void)user_data;
    boot_flash_error_t err = boot_flash_erase_partition(&s_flash_ctx,
        s_target_partition);
    return (uint8_t)err;
}

static uint8_t set_flag_cb(void* user_data, uint8_t boot_partition,
    uint16_t version, uint32_t fw_size, uint32_t fw_crc32)
{
    boot_metadata_t meta;
    (void)user_data;

    memset(&meta, 0, sizeof(meta));
    meta.magic = BOOT_METADATA_MAGIC;
    meta.boot_partition = boot_partition;
    meta.upgrade_flag = 0U; /* 升级完成 */
    meta.version = version;
    meta.fw_size = fw_size;
    meta.fw_crc32 = fw_crc32;

    boot_flash_error_t err = boot_flash_write_metadata(&s_flash_ctx, &meta);
    return (uint8_t)err;
}

static void reset_cb(void* user_data)
{
    (void)user_data;
    LOG_I("boot", "system reset...");
    HAL_NVIC_SystemReset();
}
