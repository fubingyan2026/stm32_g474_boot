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
#include "fdcan.h"
#include "fsm.h"
#include "log.h"
#include "msg_fifo.h"
#include "sw_timer.h"

/* Private constants ---------------------------------------------------------*/

/** @brief 本文件日志开关：置 0 屏蔽本文件全部打印 */
#define BOOT_TASK_LOG_ENABLE 0

#if BOOT_TASK_LOG_ENABLE
#define BOOT_TASK_LOG_E(...) LOG_E("boot_task", __VA_ARGS__)
#define BOOT_TASK_LOG_W(...) LOG_W("boot_task", __VA_ARGS__)
#define BOOT_TASK_LOG_I(...) LOG_I("boot_task", __VA_ARGS__)
#define BOOT_TASK_LOG_D(...) LOG_D("boot_task", __VA_ARGS__)
#else
#define BOOT_TASK_LOG_E(...) ((void)0)
#define BOOT_TASK_LOG_W(...) ((void)0)
#define BOOT_TASK_LOG_I(...) ((void)0)
#define BOOT_TASK_LOG_D(...) ((void)0)
#endif

/** CAN RX 消息队列容量 */
#define BOOT_MSG_FIFO_SIZE 256U

/** 主循环轮询周期 (ms) */
#define BOOT_TASK_PERIOD_MS 1U

/** Bus-Off 轮询周期 (ms)，以主循环周期为 tick 累加 */
#define BOOT_BUSOFF_POLL_MS 100U

/* Private variables ---------------------------------------------------------*/

/* CAN 消息队列 */
static drv_can_msg_t s_msg_fifo_buffer[BOOT_MSG_FIFO_SIZE];
static msg_fifo_t s_msg_fifo;

/* Flash 分区管理器 */
static boot_flash_context_t s_flash_ctx;
static bool s_flash_inited = false;

/* 状态机实例 */
static fsm_t s_fsm;
static boot_fsm_context_t s_fsm_ctx;

/* 定时器 */
static sw_timer_t s_timer;

/* 目标分区 */
static boot_partition_t s_target_partition = BOOT_PARTITION_A;

/* Bus-Off 轮询降频计数器 */
static uint32_t s_busoff_poll_tick = 0U;

/* Private function prototypes -----------------------------------------------*/

static void can_rx_callback(drv_can_channel_t ch, const drv_can_msg_t* msg);
static void boot_timer_cb(void* user_data);

/* 回调函数 */
static uint8_t write_block_cb(void* user_data, uint32_t offset,
    const uint8_t* data, uint32_t len);
static uint8_t verify_block_cb(void* user_data, uint32_t offset,
    const uint8_t* data, uint32_t len);
static uint8_t verify_fw_cb(void* user_data, uint32_t size, uint32_t* checksum);
static uint8_t erase_cb(void* user_data);
static uint8_t set_flag_cb(void* user_data, uint8_t boot_partition,
    uint16_t version, uint32_t fw_size, uint32_t fw_checksum);
static void reset_cb(void* user_data);

/* Exported functions --------------------------------------------------------*/

bool boot_task_try_boot_app(void)
{
    boot_metadata_t meta;

    if (!s_flash_inited) {
        boot_flash_init(&s_flash_ctx);
        s_flash_inited = true;
    }
    boot_flash_read_metadata(&s_flash_ctx, &meta);

    /* 检查 Metadata 有效性 */
    if (meta.magic != BOOT_METADATA_MAGIC) {
        BOOT_TASK_LOG_I( "未找到有效 Metadata，进入 Bootloader 模式");
        return false;
    }

    if (meta.upgrade_flag != 0U) {
        BOOT_TASK_LOG_I( "升级标志置位，进入 Bootloader 模式 (flag=%u)", meta.upgrade_flag);
        return false;
    }

    BOOT_TASK_LOG_I( "Metadata 有效: 分区=%c, 版本=%u, 大小=%u, checksum=0x%08lX",
        (meta.boot_partition == BOOT_PARTITION_A) ? 'A' : 'B',
        meta.version, meta.fw_size, meta.fw_checksum);

    /* 校验和验证 App 分区 */
    uint32_t calculated_checksum;
    boot_partition_t part = (meta.boot_partition == BOOT_PARTITION_A)
        ? BOOT_PARTITION_A
        : BOOT_PARTITION_B;
    if (boot_flash_compute_checksum(&s_flash_ctx, part,
            meta.fw_size, &calculated_checksum)
        != BOOT_FLASH_OK) {
        BOOT_TASK_LOG_E( "分区 %c 校验和计算失败，进入 Bootloader", (part == BOOT_PARTITION_A) ? 'A' : 'B');
        return false;
    }

    if (calculated_checksum != meta.fw_checksum) {
        BOOT_TASK_LOG_E( "分区 %c 校验和不匹配: 期望 0x%08lX, 计算 0x%08lX, 进入 Bootloader",
            (part == BOOT_PARTITION_A) ? 'A' : 'B', meta.fw_checksum, calculated_checksum);
        return false;
    }

    /* 跳转到 App */
    BOOT_TASK_LOG_I( "校验和验证通过，跳转到分区 %c, 版本=%u",
        (part == BOOT_PARTITION_A) ? 'A' : 'B', meta.version);

    // uint32_t app_addr = boot_flash_partition_addr(part);
    // uint32_t app_sp = *(volatile uint32_t*)app_addr;
    // uint32_t app_pc = *(volatile uint32_t*)(app_addr + 4U);

    // /* 设置 MSP 并跳转 */
    // __set_MSP(app_sp);
    // ((void (*)(void))app_pc)();

    /* 不应到达此处 */
    return false;
}

void boot_task_init(void)
{
    boot_flash_error_t flash_err;
    boot_fsm_config_t fsm_config;
    drv_can_error_t can_err;

    BOOT_TASK_LOG_I( "初始化 Bootloader...");

    /* 初始化消息队列 */
    msg_fifo_init(&s_msg_fifo, s_msg_fifo_buffer,
        sizeof(s_msg_fifo_buffer), sizeof(drv_can_msg_t));
    BOOT_TASK_LOG_D( "消息队列已初始化 (%u x %u 字节)",
        BOOT_MSG_FIFO_SIZE, (uint32_t)sizeof(drv_can_msg_t));

    /* 2. 初始化 Flash（如已在 try_boot_app 中初始化则跳过） */
    if (!s_flash_inited) {
        flash_err = boot_flash_init(&s_flash_ctx);
        if (flash_err != BOOT_FLASH_OK) {
            BOOT_TASK_LOG_E( "Flash 初始化失败: err=%d", flash_err);
            return;
        }
        s_flash_inited = true;
    }
    BOOT_TASK_LOG_D( "Flash 管理器已初始化");

    /* 读取 Metadata，确定目标升级分区（A/B 切换） */
    {
        boot_metadata_t meta;
        boot_flash_read_metadata(&s_flash_ctx, &meta);
        if (meta.magic == BOOT_METADATA_MAGIC && meta.upgrade_flag == 0U) {
            /* 有有效 App → 升级到相反分区 */
            s_target_partition = (meta.boot_partition == BOOT_PARTITION_A)
                ? BOOT_PARTITION_B
                : BOOT_PARTITION_A;
            BOOT_TASK_LOG_I( "当前分区 %c, 目标分区 %c",
                'A' + meta.boot_partition, 'A' + s_target_partition);
        } else {
            /* 无有效 App → 默认写到 A */
            s_target_partition = BOOT_PARTITION_A;
            BOOT_TASK_LOG_I( "无有效 App, 目标分区 A");
        }
    }

    /* 初始化 CAN（通道 1） */
    can_err = drv_can_init(DRV_CAN_CH_1, &hfdcan1);
    if (can_err != DRV_CAN_OK) {
        BOOT_TASK_LOG_E( "CAN 初始化失败: err=%d", can_err);
        return;
    }
    drv_can_register_rx_callback(DRV_CAN_CH_1, can_rx_callback);
    BOOT_TASK_LOG_D( "CAN 已初始化 (FDCAN1, PA11/PA12)");

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
    fsm_config.target_partition = (uint8_t)s_target_partition;

    if (!boot_fsm_init(&s_fsm_ctx, &s_fsm, &fsm_config)) {
        BOOT_TASK_LOG_E( "FSM 状态机初始化失败");
        return;
    }
    BOOT_TASK_LOG_D( "升级状态机已初始化 (HW_ID=0x%04X)", fsm_config.hw_compat_id);

    /* 创建轮询定时器 */
    sw_timer_init(&s_timer,
        &(sw_timer_config_t) {
            .priority = SW_TIMER_PRIO_NORMAL,
            .callback = boot_timer_cb,
            .user_data = NULL });
    sw_timer_start(&s_timer, BOOT_TASK_PERIOD_MS, 0); /* 0 = 无限重复 */
    BOOT_TASK_LOG_D( "轮询定时器已启动 (%u ms 周期)", BOOT_TASK_PERIOD_MS);

    BOOT_TASK_LOG_I( "Bootloader 就绪，等待 CAN 升级指令...");
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

    /* 补发 tick 内生成的响应（如块级看门狗超时 NACK） */
    if (boot_fsm_get_response(&s_fsm_ctx, &response)) {
        drv_can_send(DRV_CAN_CH_1, &response);
    }

    /* CAN Bus-Off 自恢复：每 ~100ms 轮询一次 */
    s_busoff_poll_tick++;
    if (s_busoff_poll_tick >= BOOT_BUSOFF_POLL_MS) {
        s_busoff_poll_tick = 0U;
        if (drv_can_is_bus_off(DRV_CAN_CH_1)) {
            BOOT_TASK_LOG_W( "检测到 CAN Bus-Off，自动恢复");
            drv_can_recover(DRV_CAN_CH_1);
        }
    }
}

/* ===== 回调函数实现 ======================================================= */

static uint8_t write_block_cb(void* user_data, uint32_t offset,
    const uint8_t* data, uint32_t len)
{
    (void)user_data;
    (void)data;
    boot_flash_error_t err = boot_flash_write_block(&s_flash_ctx,
        s_target_partition, offset, data, len);
    if (err != BOOT_FLASH_OK) {
        BOOT_TASK_LOG_E( "Flash 写入失败: err=%d, offset=%lu, len=%lu", err, offset, len);
    }
    return (uint8_t)err;
}

static uint8_t verify_block_cb(void* user_data, uint32_t offset,
    const uint8_t* data, uint32_t len)
{
    (void)user_data;
    (void)data;
    boot_flash_error_t err = boot_flash_verify_block(&s_flash_ctx,
        s_target_partition, offset, data, len);
    if (err != BOOT_FLASH_OK) {
        BOOT_TASK_LOG_E( "Flash 读回校验失败: err=%d, offset=%lu", err, offset);
    }
    return (uint8_t)err;
}

static uint8_t verify_fw_cb(void* user_data, uint32_t size, uint32_t* checksum)
{
    (void)user_data;
    boot_flash_error_t err = boot_flash_compute_checksum(&s_flash_ctx,
        s_target_partition, size, checksum);
    if (err != BOOT_FLASH_OK) {
        BOOT_TASK_LOG_E( "Checksum 计算失败: err=%d, size=%lu", err, size);
    }
    return (uint8_t)err;
}

static uint8_t erase_cb(void* user_data)
{
    (void)user_data;
    boot_flash_error_t err = boot_flash_erase_partition(&s_flash_ctx,
        s_target_partition);
    if (err != BOOT_FLASH_OK) {
        BOOT_TASK_LOG_E( "分区擦除失败: err=%d, partition=%c",
            err, 'A' + s_target_partition);
    }
    return (uint8_t)err;
}

static uint8_t set_flag_cb(void* user_data, uint8_t boot_partition,
    uint16_t version, uint32_t fw_size, uint32_t fw_checksum)
{
    boot_metadata_t meta;
    (void)user_data;

    memset(&meta, 0, sizeof(meta));
    meta.magic = BOOT_METADATA_MAGIC;
    meta.boot_partition = boot_partition;
    meta.upgrade_flag = 0U; /* 升级完成 */
    meta.version = version;
    meta.fw_size = fw_size;
    meta.fw_checksum = fw_checksum;

    boot_flash_error_t err = boot_flash_write_metadata(&s_flash_ctx, &meta);
    if (err != BOOT_FLASH_OK) {
        BOOT_TASK_LOG_E( "Metadata 写入失败: err=%d, part=%c, ver=%u",
            err, 'A' + boot_partition, version);
    } else {
        BOOT_TASK_LOG_I( "Metadata写入成功.");
    }
    return (uint8_t)err;
}

static void reset_cb(void* user_data)
{
    (void)user_data;
    BOOT_TASK_LOG_I( "执行系统复位...");
    // HAL_NVIC_SystemReset();
}
