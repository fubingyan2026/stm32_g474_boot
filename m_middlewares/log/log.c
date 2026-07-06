/**
 * @file    log.c
 * @author  G1_Hand 项目组
 * @version V1.0.0
 * @date    2026 6 12
 * @brief   日志模块实现（ESP32风格日志输出）
 * @attention
 *
 * Copyright (c) 2026 G1_Hand 项目组.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Private constants ---------------------------------------------------------*/

/** @brief 日志级别对应的单字符标识 */
static const char* const s_level_chars[] = {
    "N", /**< NONE */
    "E", /**< ERROR */
    "W", /**< WARN */
    "I", /**< INFO */
    "D", /**< DEBUG */
    "T", /**< TRACE */
};

/** @brief 日志级别对应的 ANSI 颜色码 */
static const char* const s_level_colors[] = {
    LOG_COLOR_RESET, /**< NONE */
    LOG_COLOR_RED, /**< ERROR - 红色 */
    LOG_COLOR_YELLOW, /**< WARN - 黄色 */
    LOG_COLOR_GREEN, /**< INFO - 绿色 */
    LOG_COLOR_BLUE, /**< DEBUG - 蓝色 */
    LOG_COLOR_RESET, /**< TRACE - 无颜色 */
};

/* Private variables ---------------------------------------------------------*/

/** @brief 格式化缓冲区 */
static char s_format_buffer[LOG_DEFAULT_FORMAT_BUFFER_SIZE];

/** @brief 输出 FIFO 缓冲区 */
static uint8_t s_log_tx_buffer[LOG_DEFAULT_TX_BUFFER_SIZE];

/** @brief 输出 FIFO 实例 */
static kfifo_t s_tx_fifo;

/** @brief 当前日志级别 */
static uint8_t s_current_level = LOG_DEFAULT_LEVEL;

/** @brief 初始化标志 */
static bool s_initialized = false;

/** @brief 模块配置实例 */
static log_config_t s_config = {
    .name = "log",
    .tx_buffer = s_log_tx_buffer,
    .tx_buffer_size = sizeof(s_log_tx_buffer),
    .get_timestamp_cb = NULL,
    .format_buffer_size = LOG_DEFAULT_FORMAT_BUFFER_SIZE,
    .default_level = LOG_DEFAULT_LEVEL,
    .enable_color = LOG_DEFAULT_ENABLE_COLOR,
    .enable_timestamp = LOG_DEFAULT_ENABLE_TIMESTAMP,
};

/* Private function prototypes -----------------------------------------------*/

/**
 * @brief 格式化并输出日志到 FIFO
 * @param level 日志级别
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param args 可变参数列表
 * @return 操作结果错误码
 */
static log_error_t log_format_output(log_level_t level, const char* tag,
    const char* fmt, va_list args);

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化日志模块
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
log_error_t log_init(const log_config_t* config)
{
    if (!config) {
        return LOG_ERROR_NULL_PTR;
    }

    if (s_initialized) {
        log_deinit();
    }

    if (config->get_timestamp_cb) {
        s_config.get_timestamp_cb = config->get_timestamp_cb;
    }

    if (config->name) {
        s_config.name = config->name;
    }

    kfifo_init(&s_tx_fifo, s_config.tx_buffer, s_config.tx_buffer_size, NULL);

    s_initialized = true;

    return LOG_OK;
}

/**
 * @brief 反初始化日志模块
 */
void log_deinit(void)
{
    s_config.name = "log";
    s_config.tx_buffer = NULL;
    s_config.get_timestamp_cb = NULL;
    s_config.tx_buffer_size = 0;
    s_config.format_buffer_size = LOG_DEFAULT_FORMAT_BUFFER_SIZE;
    s_config.default_level = LOG_DEFAULT_LEVEL;
    s_config.enable_color = LOG_DEFAULT_ENABLE_COLOR;
    s_config.enable_timestamp = LOG_DEFAULT_ENABLE_TIMESTAMP;
    s_current_level = LOG_DEFAULT_LEVEL;
    kfifo_reset(&s_tx_fifo);
    s_initialized = false;
}

/**
 * @brief 检查日志模块是否已初始化
 * @return true表示已初始化，false表示未初始化
 */
bool log_is_initialized(void)
{
    return s_initialized;
}

/**
 * @brief 设置日志级别
 * @param level 日志级别
 * @return 操作结果错误码
 */
log_error_t log_set_level(log_level_t level)
{
    if (!s_initialized) {
        return LOG_ERROR_UNINITIALIZED;
    }

    if (level > LOG_LEVEL_TRACE) {
        return LOG_ERROR_INVALID_PARAM;
    }

    s_current_level = level;

    return LOG_OK;
}

/**
 * @brief 获取当前日志级别
 * @return 当前日志级别
 */
log_level_t log_get_level(void)
{
    if (!s_initialized) {
        return LOG_LEVEL_NONE;
    }

    return (log_level_t)s_current_level;
}

/**
 * @brief 启用或禁用颜色输出
 * @param enable true启用，false禁用
 * @return 操作结果错误码
 */
log_error_t log_set_color_enable(bool enable)
{
    if (!s_initialized) {
        return LOG_ERROR_UNINITIALIZED;
    }

    s_config.enable_color = enable;

    return LOG_OK;
}

/**
 * @brief 启用或禁用时间戳
 * @param enable true启用，false禁用
 * @return 操作结果错误码
 */
log_error_t log_set_timestamp_enable(bool enable)
{
    if (!s_initialized) {
        return LOG_ERROR_UNINITIALIZED;
    }

    s_config.enable_timestamp = enable;

    return LOG_OK;
}

/**
 * @brief 日志输出
 * @param level 日志级别
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @return 操作结果错误码
 */
log_error_t log_log(log_level_t level, const char* tag, const char* fmt, ...)
{
    if (!fmt) {
        return LOG_ERROR_NULL_PTR;
    }

    if (!s_initialized) {
        return LOG_ERROR_UNINITIALIZED;
    }

    if (level > s_current_level) {
        return LOG_OK;
    }

    va_list args;
    va_start(args, fmt);
    log_error_t result = log_format_output(level, tag, fmt, args);
    va_end(args);

    return result;
}

/**
 * @brief 十六进制转储输出
 * @param tag 日志标签
 * @param data 数据指针
 * @param len 数据长度
 * @return 操作结果错误码
 */
log_error_t log_hexdump(const char* tag, const uint8_t* data, uint32_t len)
{
    (void)tag;

    if (!data) {
        return LOG_ERROR_NULL_PTR;
    }

    if (!s_initialized) {
        return LOG_ERROR_UNINITIALIZED;
    }

    if (len == 0) {
        return LOG_OK;
    }

    char* buf = s_format_buffer;
    uint16_t buf_size = s_config.format_buffer_size;
    uint32_t offset = 0;

    for (uint32_t i = 0; i < len; i += 16) {
        offset = 0;

        offset += snprintf(buf + offset, buf_size - offset, "%04lX: ", (unsigned long)i);

        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < len) {
                offset += snprintf(buf + offset, buf_size - offset, "%02X ", data[i + j]);
            } else {
                offset += snprintf(buf + offset, buf_size - offset, "   ");
            }

            if (j == 7) {
                offset += snprintf(buf + offset, buf_size - offset, " ");
            }
        }

        offset += snprintf(buf + offset, buf_size - offset, " |");

        for (uint32_t j = 0; j < 16 && i + j < len; j++) {
            char c = (char)data[i + j];
            if (c >= 0x20 && c <= 0x7E) {
                offset += snprintf(buf + offset, buf_size - offset, "%c", c);
            } else {
                offset += snprintf(buf + offset, buf_size - offset, ".");
            }
        }

        offset += snprintf(buf + offset, buf_size - offset, "|\r\n");

        kfifo_put(&s_tx_fifo, (const uint8_t*)buf, offset);
    }

    return LOG_OK;
}

/**
 * @brief 原始数据输出（不添加格式）
 * @param data 数据指针
 * @param len 数据长度
 * @return 操作结果错误码
 */
log_error_t log_write(const uint8_t* data, uint32_t len)
{
    if (!data) {
        return LOG_ERROR_NULL_PTR;
    }

    if (!s_initialized) {
        return LOG_ERROR_UNINITIALIZED;
    }

    if (len == 0) {
        return LOG_OK;
    }

    kfifo_put(&s_tx_fifo, data, len);

    return LOG_OK;
}

/**
 * @brief 获取输出缓冲区中的数据长度
 * @return 缓冲区中的数据长度，未初始化返回0
 */
uint32_t log_tx_len(void)
{
    if (!s_initialized) {
        return 0;
    }

    return kfifo_len(&s_tx_fifo);
}

/**
 * @brief 从输出缓冲区取出数据
 * @param buffer 目标缓冲区
 * @param len 期望取出的长度
 * @return 实际取出的长度
 */
uint32_t log_tx_get(uint8_t* buffer, uint32_t len)
{
    if (!buffer || len == 0) {
        return 0;
    }

    if (!s_initialized) {
        return 0;
    }

    return kfifo_get(&s_tx_fifo, buffer, len);
}

/**
 * @brief 断言失败处理函数
 * @param func 函数名
 * @param line 行号
 */
void log_assert_failed(const char* func, uint32_t line)
{
    LOG_E("ASSERT FAILED: %s:%lu", func ? func : "unknown", (unsigned long)line);
    for (;;) {
    }
}

/**
 * @brief 格式化并输出日志到 FIFO
 * @param level 日志级别
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param args 可变参数列表
 * @return 操作结果错误码
 */
static log_error_t log_format_output(log_level_t level, const char* tag,
    const char* fmt, va_list args)
{
    char* buf = s_format_buffer;
    uint16_t buf_size = s_config.format_buffer_size;
    uint32_t offset = 0;

    if (level > LOG_LEVEL_TRACE) {
        level = LOG_LEVEL_TRACE;
    }

    const char* level_char = s_level_chars[level];
    const char* level_color = s_level_colors[level];

    if (tag == NULL) {
        tag = s_config.name;
    }

    if (s_config.enable_color) {
        offset += snprintf(buf + offset, buf_size - offset, "%s", level_color);
    }

    offset += snprintf(buf + offset, buf_size - offset, "%s (", level_char);

    if (s_config.enable_timestamp) {
        static uint32_t timestamp = 0;
        if (s_config.get_timestamp_cb) {
            timestamp = s_config.get_timestamp_cb();
        } else {
            timestamp++;
        }
        offset += snprintf(buf + offset, buf_size - offset, "%lu",
            (unsigned long)timestamp);
    }

    offset += snprintf(buf + offset, buf_size - offset, ") %s: ", tag);

    int msg_len = vsnprintf(buf + offset, buf_size - offset, fmt, args);
    if (msg_len < 0) {
        return LOG_ERROR_INVALID_PARAM;
    }

    offset += msg_len;
    if (offset >= buf_size) {
        offset = buf_size - 1;
    }

    if (s_config.enable_color) {
        offset += snprintf(buf + offset, buf_size - offset, "%s", LOG_COLOR_RESET);
    }

    offset += snprintf(buf + offset, buf_size - offset, "\r\n");

    kfifo_put(&s_tx_fifo, (const uint8_t*)buf, offset);

    return LOG_OK;
}
