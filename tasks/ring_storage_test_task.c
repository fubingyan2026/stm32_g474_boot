/**
 * @file    ring_storage_test_task.c
 * @brief   ring_storage 模块功能测试任务实现
 *
 * 上电自测试序列，验证 save / load / 重启持久化 / 版本递增。
 * 测试区域：Flash 末尾 0x0801C000 ~ 0x0801DFFF（8KB，安全空闲区域）
 */

/* Includes ------------------------------------------------------------------*/
#include "ring_storage_test_task.h"

#include <string.h>

#include "log.h"
#include "ring_storage.h"
#include "ring_storage_port.h"
#include "drv_stm32g4_flash.h"

/* Private constants ---------------------------------------------------------*/

/** @brief 本文件日志开关：置 0 屏蔽本文件全部打印 */
#define RS_TEST_LOG_ENABLE 1

#if RS_TEST_LOG_ENABLE
#define RS_TEST_LOG_E(...) LOG_E("rs_test", __VA_ARGS__)
#define RS_TEST_LOG_W(...) LOG_W("rs_test", __VA_ARGS__)
#define RS_TEST_LOG_I(...) LOG_I("rs_test", __VA_ARGS__)
#define RS_TEST_LOG_D(...) LOG_D("rs_test", __VA_ARGS__)
#else
#define RS_TEST_LOG_E(...) ((void)0)
#define RS_TEST_LOG_W(...) ((void)0)
#define RS_TEST_LOG_I(...) ((void)0)
#define RS_TEST_LOG_D(...) ((void)0)
#endif

/** Flash 测试区域：Metadata 之后，Flash 末尾的空闲空间（16KB 中取前 8KB） */
#define RS_TEST_START_ADDR      0x0801C000U
#define RS_TEST_AREA_SIZE       8192U       /* 8KB */
#define RS_TEST_SECTOR_SIZE     RING_STORAGE_SECTOR_4K       /* 4KB，兼容单/双 Bank 模式 */

/** 帧缓冲区 */
#define RS_TEST_FRAME_BUF_SIZE  98U

/* Private variables ---------------------------------------------------------*/

static uint8_t s_frame_buf[RS_TEST_FRAME_BUF_SIZE];
static ring_storage_context_t s_storage;

/* 测试用参数（默认值） */
static uint8_t  g_motor_poles = 11;
static int32_t    g_pid_kp    = 666;
static int32_t    g_pid_ki    = 16;
static uint32_t g_uart_baud   = 115200U;
static uint32_t g_boot_count  = 0U;     /**< MCU 上电启动次数 */

/* Private functions prototypes ----------------------------------------------*/

static void register_all_kv(ring_storage_context_t* ctx);
static void clear_ram_values(void);
static void verify_values(const char* stage, uint8_t exp_poles,
                          int32_t exp_kp, int32_t exp_ki, uint32_t exp_baud);
static void test_basic_save_load(void);
static void test_persistence_after_reinit(void);
static void test_version_increment(void);
static void print_frame_buf_hex(void);

/* Private functions ---------------------------------------------------------*/

static void register_all_kv(ring_storage_context_t* ctx)
{
    ring_storage_register(ctx, "motor_poles", &g_motor_poles, sizeof(g_motor_poles));
    ring_storage_register(ctx, "pid_kp",       &g_pid_kp,      sizeof(g_pid_kp));
    ring_storage_register(ctx, "pid_ki",       &g_pid_ki,      sizeof(g_pid_ki));
    ring_storage_register(ctx, "uart_baud",    &g_uart_baud,   sizeof(g_uart_baud));
    ring_storage_register(ctx, "boot_count",   &g_boot_count,  sizeof(g_boot_count));
}

static void clear_ram_values(void)
{
    g_motor_poles = 0;
    g_pid_kp      = 0;
    g_pid_ki      = 0;
    g_uart_baud   = 0;
}

static void verify_values(const char* stage, uint8_t exp_poles,
                          int32_t exp_kp, int32_t exp_ki, uint32_t exp_baud)
{
    bool pass = true;

    if (g_motor_poles != exp_poles) {
        RS_TEST_LOG_E( "[%s] motor_poles 不匹配: 期望 %u, 实际 %u",
              stage, exp_poles, g_motor_poles);
        pass = false;
    }
    if (g_pid_kp != exp_kp) {
        RS_TEST_LOG_E( "[%s] pid_kp 不匹配: 期望 %ld, 实际 %ld",
              stage, (long)exp_kp, (long)g_pid_kp);
        pass = false;
    }
    if (g_pid_ki != exp_ki) {
        RS_TEST_LOG_E( "[%s] pid_ki 不匹配: 期望 %ld, 实际 %ld",
              stage, (long)exp_ki, (long)g_pid_ki);
        pass = false;
    }
    if (g_uart_baud != exp_baud) {
        RS_TEST_LOG_E( "[%s] uart_baud 不匹配: 期望 %lu, 实际 %lu",
              stage, (unsigned long)exp_baud, (unsigned long)g_uart_baud);
        pass = false;
    }

    if (pass) {
        RS_TEST_LOG_I( "[%s] ✓ 全部正确 (ver=%u, poles=%u, kp=%ld, ki=%ld, baud=%lu)",
              stage, s_storage.latest_version,
              g_motor_poles, (long)g_pid_kp, (long)g_pid_ki, (unsigned long)g_uart_baud);
    } else {
        RS_TEST_LOG_E( "[%s] ✗ 校验失败", stage);
    }
}

static void print_frame_buf_hex(void)
{
    /* 仅在首次 save 后打印帧缓冲区前 64 字节，用于调试 */
    RS_TEST_LOG_I( "帧缓冲区前 64 字节:");
    for (int i = 0; i < 4; i++) {
        RS_TEST_LOG_I( "  [%02d]: %02X %02X %02X %02X %02X %02X %02X %02X "
                           "%02X %02X %02X %02X %02X %02X %02X %02X",
              i * 16,
              s_frame_buf[i * 16 + 0],  s_frame_buf[i * 16 + 1],
              s_frame_buf[i * 16 + 2],  s_frame_buf[i * 16 + 3],
              s_frame_buf[i * 16 + 4],  s_frame_buf[i * 16 + 5],
              s_frame_buf[i * 16 + 6],  s_frame_buf[i * 16 + 7],
              s_frame_buf[i * 16 + 8],  s_frame_buf[i * 16 + 9],
              s_frame_buf[i * 16 + 10], s_frame_buf[i * 16 + 11],
              s_frame_buf[i * 16 + 12], s_frame_buf[i * 16 + 13],
              s_frame_buf[i * 16 + 14], s_frame_buf[i * 16 + 15]);
    }
}

/* 测试用例 ------------------------------------------------------------------*/

/**
 * @brief 测试 2：重新初始化（模拟重启）后数据持久化
 */
static void test_persistence_after_reinit(void)
{
    RS_TEST_LOG_I( "──── 测试 2: 重启持久化 ────");

    /* 不擦除 Flash，模拟重启 */
    memset(&s_storage, 0, sizeof(s_storage));

    ring_storage_error_t err = ring_storage_init(&s_storage, &(ring_storage_config_t){
        .start_addr        = RS_TEST_START_ADDR,
        .area_size         = RS_TEST_AREA_SIZE,
        .sector_size       = RS_TEST_SECTOR_SIZE,
        .write_gran        = RING_STORAGE_WRITE_GRAN_64,
        .frame_buffer      = s_frame_buf,
        .frame_buffer_size = RS_TEST_FRAME_BUF_SIZE,
    });

    if (err != RING_STORAGE_OK) {
        RS_TEST_LOG_E( "重启后 init 失败: err=%d", err);
        return;
    }

    /* 必须重新注册 KV（上下文被清零了） */
    register_all_kv(&s_storage);

    clear_ram_values();
    err = ring_storage_load(&s_storage);
    if (err != RING_STORAGE_OK) {
        RS_TEST_LOG_E( "重启后 load 失败: err=%d", err);
        return;
    }

    /* 验证还是测试 1 最后一次 save 的值 */
    verify_values("T2-reboot", 22, 666, 16, 921600U);

    RS_TEST_LOG_I( "测试 2 通过 ✓");
}

/**
 * @brief 测试 3：多次 save 版本递增
 */
static void test_version_increment(void)
{
    RS_TEST_LOG_I( "──── 测试 3: 版本递增 ────");

    uint32_t expected_ver = s_storage.latest_version;

    for (int i = 0; i < 5; i++) {
        g_motor_poles = 30 + (uint8_t)i;
        g_pid_kp      = 3 + i * 2;

        ring_storage_error_t err = ring_storage_save(&s_storage);
        if (err != RING_STORAGE_OK) {
            RS_TEST_LOG_E( "第 %d 次 save 失败: err=%d", i, err);
            return;
        }
        expected_ver++;

        if (s_storage.latest_version != expected_ver) {
            RS_TEST_LOG_E( "版本号不匹配: 期望 %lu, 实际 %lu",
                  (unsigned long)expected_ver,
                  (unsigned long)s_storage.latest_version);
            return;
        }
    }

    /* 加载验证最后一次的值 */
    clear_ram_values();
    ring_storage_load(&s_storage);
    verify_values("T3-version", 34, 674, 16, 921600U);

    RS_TEST_LOG_I( "测试 3 通过 ✓ (最终版本 %lu)",
          (unsigned long)s_storage.latest_version);
}

/* Exported functions --------------------------------------------------------*/

void ring_storage_test_task_init(void)
{
    ring_storage_error_t err;

    ef_port_init();
    RS_TEST_LOG_I( "========================================");
    RS_TEST_LOG_I( "  ring_storage 功能测试开始");
    RS_TEST_LOG_I( "  测试区域: 0x%08lX, %lu 字节",
          (unsigned long)RS_TEST_START_ADDR, (unsigned long)RS_TEST_AREA_SIZE);
    RS_TEST_LOG_I( "========================================");

    /* ──── 上电启动次数记录 ──── */
    {
        const ring_storage_config_t bc_cfg = {
            .start_addr        = RS_TEST_START_ADDR,
            .area_size         = RS_TEST_AREA_SIZE,
            .sector_size       = RS_TEST_SECTOR_SIZE,
            .write_gran        = RING_STORAGE_WRITE_GRAN_64,
            .frame_buffer      = s_frame_buf,
            .frame_buffer_size = RS_TEST_FRAME_BUF_SIZE,
        };

        memset(&s_storage, 0, sizeof(s_storage));
        err = ring_storage_init(&s_storage, &bc_cfg);
        if (err == RING_STORAGE_OK || err == RING_STORAGE_ERROR_NO_VALID_FRAME) {
            register_all_kv(&s_storage);

            err = ring_storage_load(&s_storage);
            if (err == RING_STORAGE_ERROR_NO_VALID_FRAME) {
                g_boot_count = 1U;
            } else if (err == RING_STORAGE_OK) {
                g_boot_count++;
            }

            // g_motor_poles = 11;
            // g_pid_kp    = 666;
            // g_pid_ki    = 16;
            // g_uart_baud   = 115200U;

            if (err == RING_STORAGE_OK || err == RING_STORAGE_ERROR_NO_VALID_FRAME) {
                ring_storage_save(&s_storage);
            }
        }

        RS_TEST_LOG_I( "MCU 上电启动次数: %lu", (unsigned long)g_boot_count);

        /* 打印各参数当前值 */
        RS_TEST_LOG_I( "参数: motor_poles=%u, pid_kp=%ld, pid_ki=%ld, uart_baud=%lu, boot_count=%lu",
              g_motor_poles,
              (long)g_pid_kp,
              (long)g_pid_ki,
              (unsigned long)g_uart_baud,
              (unsigned long)g_boot_count);
    }

    // test_basic_save_load();
    // test_persistence_after_reinit();
    // test_version_increment();

    RS_TEST_LOG_I( "========================================");
    RS_TEST_LOG_I( "  测试完成");
    RS_TEST_LOG_I( "========================================");
}
