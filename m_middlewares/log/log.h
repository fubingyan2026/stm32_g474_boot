/**
 * @file    log.h
 * @author  G1_Hand 项目组
 * @version V1.0.0
 * @date    2025-04-12
 * @brief   日志模块（ESP32风格日志输出）
 * @attention
 *
 * Copyright (c) 2025 G1_Hand 项目组.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 *
 */

#ifndef __LOG_H
#define __LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kfifo.h"

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 日志模块错误码枚举
 */
typedef enum {
    LOG_OK = 0,                /**< 操作成功 */
    LOG_ERROR_NULL_PTR,        /**< 空指针错误 */
    LOG_ERROR_INVALID_PARAM,   /**< 无效参数 */
    LOG_ERROR_UNINITIALIZED,   /**< 未初始化 */
    LOG_ERROR_BUFFER_OVERFLOW, /**< 缓冲区溢出 */
    LOG_ERROR_FIFO_INIT,       /**< FIFO初始化失败 */
} log_error_t;

/**
 * @brief 日志级别枚举
 */
typedef enum {
    LOG_LEVEL_NONE = 0,  /**< 无日志输出 */
    LOG_LEVEL_ERROR = 1, /**< 错误级别 */
    LOG_LEVEL_WARN = 2,  /**< 警告级别 */
    LOG_LEVEL_INFO = 3,  /**< 信息级别 */
    LOG_LEVEL_DEBUG = 4, /**< 调试级别 */
    LOG_LEVEL_TRACE = 5, /**< 跟踪级别 */
} log_level_t;

/**
 * @brief 时间戳获取回调函数类型
 * @return 当前时间戳（毫秒）
 */
typedef uint32_t (*log_get_timestamp_cb_t)(void);

/**
 * @brief 日志模块配置结构体
 */
typedef struct {
    const char* name;                 /**< 模块名称（用于日志标签） */
    uint8_t* tx_buffer;               /**< 输出缓冲区指针（必须为2的幂次方大小） */
    log_get_timestamp_cb_t get_timestamp_cb; /**< 时间戳获取回调 */
    uint16_t tx_buffer_size;          /**< 输出缓冲区大小（必须为2的幂次方） */
    uint16_t format_buffer_size;      /**< 格式化缓冲区大小 */
    uint8_t default_level;            /**< 默认调试级别 */
    bool enable_color;                /**< 是否启用颜色输出 */
    bool enable_timestamp;            /**< 是否启用时间戳 */
} log_config_t;

/* Exported constants --------------------------------------------------------*/

/* ANSI颜色代码 */
#define LOG_COLOR_RESET     "\033[0m"
#define LOG_COLOR_RED       "\033[31m"
#define LOG_COLOR_GREEN     "\033[32m"
#define LOG_COLOR_YELLOW    "\033[33m"
#define LOG_COLOR_BLUE      "\033[34m"
#define LOG_COLOR_MAGENTA   "\033[35m"
#define LOG_COLOR_CYAN      "\033[36m"
#define LOG_COLOR_WHITE     "\033[37m"
#define LOG_COLOR_GRAY      "\033[90m"
#define LOG_COLOR_BOLD      "\033[1m"
#define LOG_COLOR_UNDERLINE "\033[4m"

/* 默认配置 */
#define LOG_DEFAULT_FORMAT_BUFFER_SIZE (256)
#define LOG_DEFAULT_TX_BUFFER_SIZE     (1024 * 4)
#define LOG_DEFAULT_LEVEL              LOG_LEVEL_INFO
#define LOG_DEFAULT_ENABLE_COLOR       (true)
#define LOG_DEFAULT_ENABLE_TIMESTAMP   (true)

/* Exported macro ------------------------------------------------------------*/

/**
 * @brief 断言宏
 */
#define LOG_ASSERT(expr) ((expr) ? (void)0 : log_assert_failed(__func__, __LINE__))

/**
 * @brief 日志输出宏（ESP32风格）
 *
 * 格式: [级别] (时间戳) 标签: 消息\r\n
 * 例如: I (1234) main: Application started
 */
#define LOG_E(tag, ...) log_log(LOG_LEVEL_ERROR, tag, __VA_ARGS__)

#define LOG_W(tag, ...) log_log(LOG_LEVEL_WARN, tag, __VA_ARGS__)

#define LOG_I(tag, ...) log_log(LOG_LEVEL_INFO, tag, __VA_ARGS__)

#define LOG_D(tag, ...) log_log(LOG_LEVEL_DEBUG, tag, __VA_ARGS__)

#define LOG_T(tag, ...) log_log(LOG_LEVEL_TRACE, tag, __VA_ARGS__)

/**
 * @brief 函数跟踪宏
 */
#define LOG_ENTER() \
    log_log(LOG_LEVEL_TRACE, "trace", "--> %s()", __func__)

#define LOG_EXIT() \
    log_log(LOG_LEVEL_TRACE, "trace", "<-- %s()", __func__)

#define LOG_EXIT_VAL(val) \
    log_log(LOG_LEVEL_TRACE, "trace", "<-- %s() = %ld", __func__, (long)(val))

/**
 * @brief 十六进制转储宏
 */
#define LOG_HEXDUMP(tag, data, len) log_hexdump(tag, data, len)

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化日志模块
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
log_error_t log_init(const log_config_t* config);

/**
 * @brief 反初始化日志模块
 */
void log_deinit(void);

/**
 * @brief 检查日志模块是否已初始化
 * @return true表示已初始化，false表示未初始化
 */
bool log_is_initialized(void);

/**
 * @brief 设置日志级别
 * @param level 日志级别
 * @return 操作结果错误码
 */
log_error_t log_set_level(log_level_t level);

/**
 * @brief 获取当前日志级别
 * @return 当前日志级别
 */
log_level_t log_get_level(void);

/**
 * @brief 启用或禁用颜色输出
 * @param enable true启用，false禁用
 * @return 操作结果错误码
 */
log_error_t log_set_color_enable(bool enable);

/**
 * @brief 启用或禁用时间戳
 * @param enable true启用，false禁用
 * @return 操作结果错误码
 */
log_error_t log_set_timestamp_enable(bool enable);

/**
 * @brief 日志输出
 * @param level 日志级别
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @return 操作结果错误码
 * @note 输出格式: [级别] (时间戳) 标签: 消息
 */
log_error_t log_log(log_level_t level, const char* tag, const char* fmt, ...);

/**
 * @brief 十六进制转储输出
 * @param tag 日志标签
 * @param data 数据指针
 * @param len 数据长度
 * @return 操作结果错误码
 */
log_error_t log_hexdump(const char* tag, const uint8_t* data, uint32_t len);

/**
 * @brief 原始数据输出（不添加格式）
 * @param data 数据指针
 * @param len 数据长度
 * @return 操作结果错误码
 */
log_error_t log_write(const uint8_t* data, uint32_t len);

/**
 * @brief 获取输出缓冲区中的数据长度
 * @return 缓冲区中的数据长度，未初始化返回0
 */
uint32_t log_tx_len(void);

/**
 * @brief 从输出缓冲区取出数据
 * @param buffer 目标缓冲区
 * @param len 期望取出的长度
 * @return 实际取出的长度
 */
uint32_t log_tx_get(uint8_t* buffer, uint32_t len);

/**
 * @brief 断言失败处理函数
 * @param func 函数名
 * @param line 行号
 */
void log_assert_failed(const char* func, uint32_t line);

#ifdef __cplusplus
}
#endif

#endif /* __LOG_H */
