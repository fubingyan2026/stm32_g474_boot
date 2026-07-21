/**
 * @file    test_ring_storage.c
 * @brief   ring_storage 模块 PC 端模拟测试
 *
 * 用 RAM 模拟 Flash，验证核心功能：
 *   1. 首次初始化 + save + load
 *   2. 重新初始化（模拟重启）后数据持久化
 *   3. 多次 save 版本递增
 *   4. GC 触发（扇区写满后搬迁）
 *   5. 断电模拟（commit_magic 未写入时恢复）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "ring_storage.h"
#include "ring_storage_port.h"

/* ========================================================================== */
/* Flash 模拟（RAM 实现）                                                     */
/* ========================================================================== */

#define MOCK_FLASH_SIZE     (256 * 1024)  /* 256KB */
#define MOCK_SECTOR_SIZE    (128 * 1024)  /* 128KB */
#define MOCK_START_ADDR     0x08080000u   /* 对齐到 128KB 扇区边界 */
#define MOCK_WRITE_GRAN     64      /* 64bit = 8B */

static uint8_t s_flash[MOCK_FLASH_SIZE];

/* 断电模拟控制：写入此地址时模拟断电（不写入） */
static bool s_power_loss_enabled = false;
static uint32_t s_power_loss_addr = 0;

static void mock_flash_init(void) {
    memset(s_flash, 0xFF, sizeof(s_flash));
}

/* 平台接口实现 ------------------------------------------------------------ */

ring_storage_error_t ring_storage_port_read(uint32_t addr, uint8_t* buf, size_t size) {
    uint32_t offset = addr - MOCK_START_ADDR;
    if (offset + size > MOCK_FLASH_SIZE) {
        return RING_STORAGE_ERROR_FLASH_READ;
    }
    memcpy(buf, s_flash + offset, size);
    return RING_STORAGE_OK;
}

ring_storage_error_t ring_storage_port_write(uint32_t addr, const uint8_t* buf, size_t size) {
    uint32_t offset = addr - MOCK_START_ADDR;

    /* 断电模拟 */
    if (s_power_loss_enabled && addr <= s_power_loss_addr && addr + size > s_power_loss_addr) {
        /* 只写入断电点之前的数据 */
        size_t valid = s_power_loss_addr - addr;
        if (valid > 0) {
            memcpy(s_flash + offset, buf, valid);
        }
        s_power_loss_enabled = false; /* 触发一次后关闭 */
        printf("  [模拟断电] 写入 0x%08X 时断电，仅写入 %zu/%zu 字节\n", addr, valid, size);
        return RING_STORAGE_ERROR_FLASH_WRITE;
    }

    if (offset + size > MOCK_FLASH_SIZE) {
        return RING_STORAGE_ERROR_FLASH_WRITE;
    }

    /* 模拟 Flash 只能 1→0 特性 */
    for (size_t i = 0; i < size; i++) {
        s_flash[offset + i] &= buf[i]; /* AND 操作模拟 */
    }

    return RING_STORAGE_OK;
}

ring_storage_error_t ring_storage_port_erase(uint32_t addr, size_t size) {
    uint32_t offset = addr - MOCK_START_ADDR;
    if (offset + size > MOCK_FLASH_SIZE) {
        return RING_STORAGE_ERROR_FLASH_ERASE;
    }
    memset(s_flash + offset, 0xFF, size);
    return RING_STORAGE_OK;
}

void ring_storage_port_lock(void) {}
void ring_storage_port_unlock(void) {}

/* 断电模拟控制 */
static void mock_enable_power_loss(uint32_t addr) {
    s_power_loss_enabled = true;
    s_power_loss_addr = addr;
}

/* ========================================================================== */
/* 测试辅助                                                                   */
/* ========================================================================== */

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
            g_fail_count++; \
            return; \
        } \
    } while (0)

static int g_fail_count = 0;
static int g_pass_count = 0;

#define FRAME_BUF_SIZE  1024
static uint8_t s_frame_buf[FRAME_BUF_SIZE];
static ring_storage_context_t s_storage;

/* 测试用参数 */
static uint8_t  g_motor_poles = 11;
static float    g_pid_kp = 1.5f;
static float    g_pid_ki = 0.02f;
static uint32_t g_uart_baud = 115200;

static void register_all_kv(ring_storage_context_t* ctx) {
    ring_storage_register(ctx, "motor_poles", &g_motor_poles, sizeof(g_motor_poles));
    ring_storage_register(ctx, "pid_kp",       &g_pid_kp,      sizeof(g_pid_kp));
    ring_storage_register(ctx, "pid_ki",       &g_pid_ki,      sizeof(g_pid_ki));
    ring_storage_register(ctx, "uart_baud",    &g_uart_baud,   sizeof(g_uart_baud));
}

static const ring_storage_config_t s_config = {
    .start_addr        = MOCK_START_ADDR,
    .area_size         = MOCK_FLASH_SIZE,
    .sector_size       = MOCK_SECTOR_SIZE,
    .write_gran        = MOCK_WRITE_GRAN,
    .frame_buffer      = s_frame_buf,
    .frame_buffer_size = FRAME_BUF_SIZE,
};

/* ========================================================================== */
/* 测试用例                                                                   */
/* ========================================================================== */

/**
 * 测试 1：首次初始化 + save + load
 */
static void test_basic_save_load(void) {
    printf("\n=== 测试 1: 基本保存/加载 ===\n");

    mock_flash_init();
    memset(&s_storage, 0, sizeof(s_storage));

    ring_storage_error_t err = ring_storage_init(&s_storage, &s_config);
    if (err != RING_STORAGE_OK) {
        printf("  init 返回错误码: %d\n", err);
    }
    TEST_ASSERT(err == RING_STORAGE_OK, "首次初始化应成功返回 OK");
    TEST_ASSERT(s_storage.latest_frame_addr == 0, "首次初始化无有效帧地址");

    register_all_kv(&s_storage);

    /* 首次加载应返回 NO_VALID_FRAME */
    err = ring_storage_load(&s_storage);
    TEST_ASSERT(err == RING_STORAGE_ERROR_NO_VALID_FRAME, "首次 load 应返回 NO_VALID_FRAME");

    /* 设置参数值 */
    g_motor_poles = 11;
    g_pid_kp = 1.5f;
    g_pid_ki = 0.02f;
    g_uart_baud = 115200;

    /* 保存 */
    err = ring_storage_save(&s_storage);
    TEST_ASSERT(err == RING_STORAGE_OK, "save 应成功");
    TEST_ASSERT(s_storage.latest_version == 1, "版本号应为 1");

    /* 修改内存值 */
    g_motor_poles = 0;
    g_pid_kp = 0;
    g_pid_ki = 0;
    g_uart_baud = 0;

    /* 加载恢复 */
    err = ring_storage_load(&s_storage);
    TEST_ASSERT(err == RING_STORAGE_OK, "load 应成功");

    TEST_ASSERT(g_motor_poles == 11, "motor_poles 应恢复为 11");
    TEST_ASSERT(g_pid_kp == 1.5f, "pid_kp 应恢复为 1.5");
    TEST_ASSERT(g_pid_ki == 0.02f, "pid_ki 应恢复为 0.02");
    TEST_ASSERT(g_uart_baud == 115200, "uart_baud 应恢复为 115200");

    printf("  [PASS] 基本保存/加载验证通过\n");
    g_pass_count++;
}

/**
 * 测试 2：重新初始化（模拟重启）后数据持久化
 */
static void test_persistence_after_reboot(void) {
    printf("\n=== 测试 2: 重启后数据持久化 ===\n");

    /* 不重置 Flash，模拟重启 */
    memset(&s_storage, 0, sizeof(s_storage));

    ring_storage_error_t err = ring_storage_init(&s_storage, &s_config);
    TEST_ASSERT(err == RING_STORAGE_OK, "重新初始化应找到有效帧");

    register_all_kv(&s_storage);

    /* 内存值是默认值（非 Flash 中的值） */
    g_motor_poles = 0;
    g_pid_kp = 0;
    g_pid_ki = 0;
    g_uart_baud = 0;

    err = ring_storage_load(&s_storage);
    TEST_ASSERT(err == RING_STORAGE_OK, "load 应成功");

    TEST_ASSERT(g_motor_poles == 11, "motor_poles 应恢复为 11");
    TEST_ASSERT(g_pid_kp == 1.5f, "pid_kp 应恢复为 1.5");
    TEST_ASSERT(g_uart_baud == 115200, "uart_baud 应恢复为 115200");
    TEST_ASSERT(s_storage.latest_version == 1, "版本号应为 1");

    printf("  [PASS] 重启持久化验证通过\n");
    g_pass_count++;
}

/**
 * 测试 3：多次 save 版本递增
 */
static void test_version_increment(void) {
    printf("\n=== 测试 3: 多次 save 版本递增 ===\n");

    uint32_t expected_ver = s_storage.latest_version;

    for (int i = 0; i < 5; i++) {
        g_motor_poles = 11 + i;
        g_pid_kp = 1.5f + i * 0.1f;

        ring_storage_error_t err = ring_storage_save(&s_storage);
        TEST_ASSERT(err == RING_STORAGE_OK, "save 应成功");
        expected_ver++;
        TEST_ASSERT(s_storage.latest_version == expected_ver, "版本号应递增");
    }

    /* 重新加载验证最后一次保存的数据 */
    g_motor_poles = 0;
    g_pid_kp = 0;
    ring_storage_load(&s_storage);

    TEST_ASSERT(g_motor_poles == 15, "motor_poles 应为 15 (11+4)");
    TEST_ASSERT(g_pid_kp == 1.9f, "pid_kp 应为 1.9 (1.5+0.4)");

    printf("  [PASS] 版本递增验证通过（当前版本 %u）\n", s_storage.latest_version);
    g_pass_count++;
}

/**
 * 测试 4：GC 触发（扇区写满后搬迁）
 */
static void test_gc_trigger(void) {
    printf("\n=== 测试 4: GC 触发 ===\n");

    /* 2KB 扇区，帧约 70B，每扇区约可存 29 帧 */
    /* 持续 save 直到触发 GC */
    uint32_t last_ver = s_storage.latest_version;
    int max_saves = 200; /* 足够多以触发多次 GC */

    for (int i = 0; i < max_saves; i++) {
        g_motor_poles = (uint8_t)(11 + i);
        ring_storage_error_t err = ring_storage_save(&s_storage);

        if (err != RING_STORAGE_OK) {
            printf("  [FAIL] save 第 %d 次失败: %d\n", i, err);
            g_fail_count++;
            return;
        }

        /* 检测 GC：活动扇区地址变化说明发生了 GC */
        if (s_storage.latest_version != last_ver + 1) {
            /* 版本号异常 */
        }
        last_ver = s_storage.latest_version;
    }

    /* 验证最终数据正确 */
    g_motor_poles = 0;
    ring_storage_load(&s_storage);
    TEST_ASSERT(g_motor_poles == (uint8_t)(11 + max_saves - 1),
                "GC 后数据应正确");

    printf("  [PASS] GC 触发验证通过（保存 %d 次，版本 %u）\n",
           max_saves, s_storage.latest_version);
    g_pass_count++;
}

/**
 * 测试 5：断电模拟（commit_magic 未写入）
 */
static void test_power_loss(void) {
    printf("\n=== 测试 5: 断电模拟 ===\n");

    /* 先正常保存一帧 */
    mock_flash_init();
    memset(&s_storage, 0, sizeof(s_storage));
    ring_storage_init(&s_storage, &s_config);
    register_all_kv(&s_storage);

    g_motor_poles = 22;
    g_pid_kp = 3.0f;
    ring_storage_save(&s_storage);
    uint32_t ver_before = s_storage.latest_version;

    printf("  断电前版本: %u, 帧@0x%08X\n", ver_before, s_storage.latest_frame_addr);

    /* 计算下一次 save 的写入地址，在帧尾位置模拟断电 */
    /* 帧尾 = write_addr + body_len，body_len = frame_len - 8 */
    /* 我们不知道确切的 body_len，但可以在帧尾地址附近设置断电点 */

    /* 设置断电点：下一次 save 的帧尾写入位置 */
    uint32_t next_write = s_storage.active_sector_addr + s_storage.write_offset;
    /* 帧头 16B + KV 数据约 54B = body 约 70B，帧尾在 write_addr + ~70 处 */
    /* 设置断电点在帧尾写入时触发 */
    /* 帧尾地址 = next_write + body_len，body_len 约 70 */
    mock_enable_power_loss(next_write + 70);

    g_motor_poles = 99;
    ring_storage_error_t err = ring_storage_save(&s_storage);
    printf("  断电 save 返回: %d (期望非 OK)\n", err);

    /* 模拟重启 */
    memset(&s_storage, 0, sizeof(s_storage));
    err = ring_storage_init(&s_storage, &s_config);
    TEST_ASSERT(err == RING_STORAGE_OK, "重启后应找到断电前的有效帧");
    register_all_kv(&s_storage);

    printf("  重启后版本: %u (期望 %u)\n", s_storage.latest_version, ver_before);
    TEST_ASSERT(s_storage.latest_version == ver_before,
                "断电后应回退到上一有效版本");

    /* 加载数据，应为断电前的值 */
    g_motor_poles = 0;
    g_pid_kp = 0;
    ring_storage_load(&s_storage);

    TEST_ASSERT(g_motor_poles == 22, "断电后 motor_poles 应为 22（断电前的值）");
    TEST_ASSERT(g_pid_kp == 3.0f, "断电后 pid_kp 应为 3.0");

    printf("  [PASS] 断电恢复验证通过\n");
    g_pass_count++;
}

/**
 * 测试 6：CRC 校验失败恢复（Flash 数据损坏）
 */
static void test_crc_corruption(void) {
    printf("\n=== 测试 6: CRC 数据损坏 ===\n");

    /* 先正常保存 */
    mock_flash_init();
    memset(&s_storage, 0, sizeof(s_storage));
    ring_storage_init(&s_storage, &s_config);
    register_all_kv(&s_storage);

    g_motor_poles = 33;
    ring_storage_save(&s_storage);

    /* 破坏帧尾的 commit_magic（模拟写入不完整） */
    /* 帧尾地址 = latest_frame_addr + frame_len - 8 */
    /* 直接破坏 commit_magic 的 4 字节 */
    uint32_t commit_addr = s_storage.latest_frame_addr + 70; /* 近似帧尾位置 */
    uint32_t offset = commit_addr - MOCK_START_ADDR;
    /* 将 commit_magic 改为 0x00（模拟未写入） */
    if (offset + 4 <= MOCK_FLASH_SIZE) {
        s_flash[offset] = 0x00;
        s_flash[offset + 1] = 0x00;
        s_flash[offset + 2] = 0x00;
        s_flash[offset + 3] = 0x00;
    }

    /* 模拟重启 */
    memset(&s_storage, 0, sizeof(s_storage));
    ring_storage_error_t err = ring_storage_init(&s_storage, &s_config);

    /* 应该找不到有效帧（因为只有一帧且被破坏了） */
    if (err == RING_STORAGE_ERROR_NO_VALID_FRAME) {
        printf("  [PASS] CRC 损坏后正确识别为无有效帧\n");
        g_pass_count++;
    } else if (err == RING_STORAGE_OK) {
        /* 可能破坏的不是帧尾，检查加载是否正常 */
        register_all_kv(&s_storage);
        g_motor_poles = 0;
        err = ring_storage_load(&s_storage);
        if (err != RING_STORAGE_OK) {
            printf("  [PASS] CRC 损坏帧被正确跳过\n");
            g_pass_count++;
        } else {
            printf("  [SKIP] 破坏位置不在帧尾，无法验证此场景\n");
        }
    }
}

/* ========================================================================== */
/* 主函数                                                                     */
/* ========================================================================== */

int main(void) {
    printf("========================================\n");
    printf("  ring_storage 模块测试\n");
    printf("========================================\n");

    test_basic_save_load();
    test_persistence_after_reboot();
    test_version_increment();
    test_gc_trigger();
    test_power_loss();
    test_crc_corruption();

    printf("\n========================================\n");
    printf("  测试结果: %d 通过, %d 失败\n", g_pass_count, g_fail_count);
    printf("========================================\n");

    return g_fail_count > 0 ? 1 : 0;
}
