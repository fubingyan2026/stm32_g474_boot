
/**
 * @file    protocol_parser.c
 * @author  maximilian
 * @version V1.0.0
 * @date    2026-06-02
 * @brief   通用协议解析器实现
 * @attention
 *
 * Copyright (c) 2026 Company Name.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "protocol_parser.h"

#include <string.h>

/* Private constants ---------------------------------------------------------*/

/*
 * 空闲超时计算常量。
 * FRAME_BAUD_RATE 名义上针对 UART (115200)，在 CAN-FD (1M/5M) 场景下
 * 仅作为空闲阈值公式的缩放因子复用，IDLE_TIMER_US 和 IDLE_FACTOR 联合控制实际时基。
 */
#define IDLE_TIMER_US (1000 * 5)
#define FRAME_BAUD_RATE 115200
#define IDLE_FACTOR 4

#define IDLE_FRAME_FACTOR_NUMERATOR ((1000000UL * 10 * (IDLE_FACTOR + 1)))
#define IDLE_FRAME_FACTOR_DENOMINATOR (FRAME_BAUD_RATE * IDLE_TIMER_US)

/* Private function prototypes -----------------------------------------------*/
static inline uint8_t parser_safe_access(const uint8_t* ptr, size_t idx,
    size_t max);
static void parser_print_hex(const uint8_t* data, size_t length);
static protocol_parser_error_t parser_check_header(
    const protocol_parser_context_t* ctx);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief 安全访问缓冲区
 * @param ptr 缓冲区指针
 * @param idx 索引
 * @param max 最大索引
 * @return 数据值，越界返回0
 */
static inline uint8_t parser_safe_access(const uint8_t* ptr, size_t idx,
    size_t max)
{
    return (ptr && idx < max) ? ptr[idx] : 0;
}

/**
 * @brief 打印十六进制数据
 * @param data 数据指针
 * @param length 数据长度
 */
static void parser_print_hex(const uint8_t* data, size_t length)
{
    (void)data;
    (void)length;
}

/**
 * @brief 检查帧头匹配
 * @param ctx 协议解析器上下文指针
 * @return 操作结果错误码
 */
static protocol_parser_error_t parser_check_header(
    const protocol_parser_context_t* ctx)
{
    if (ctx->config.header_len == 0) {
        return PROTOCOL_PARSER_OK;
    }

    uint8_t i = 0;
    uint8_t tmp;
    const uint16_t len = kfifo_len(&ctx->fifo);

    if (len < (ctx->config.header_len + ctx->config.footer_len + 1)) {
        return PROTOCOL_PARSER_ERROR_INCOMPLETE;
    }

    do {
        if (!kfifo_get(&ctx->fifo, &tmp, 1)) {
            break;
        }

        if (tmp == parser_safe_access(ctx->config.header, i, ctx->config.header_len)) {
            if (++i == ctx->config.header_len) {
                return PROTOCOL_PARSER_OK;
            }
        } else {
            i = 0;
        }
    } while (1);

    return PROTOCOL_PARSER_ERROR_HEADER_MISMATCH;
}

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化协议解析器
 * @param ctx 协议解析器上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_init(
    protocol_parser_context_t* ctx, const protocol_parser_config_t* config)
{
    // 检查参数有效性
    if (!ctx || !config) {
        return PROTOCOL_PARSER_ERROR_NULL_PTR;
    }

    if (!config->output_buffer || !config->input_buffer) {
        return PROTOCOL_PARSER_ERROR_NULL_PTR;
    }

    if (config->input_buffer_len == 0) {
        return PROTOCOL_PARSER_ERROR_BUFFER_TOO_SMALL;
    }

    uint16_t header_len = config->header_len;
    uint16_t footer_len = config->footer_len;

    // 处理空指针情况
    if (config->header == NULL) {
        header_len = 0;
    }
    if (config->footer == NULL) {
        footer_len = 0;
    }

    // 检查缓冲区大小
    if (config->output_buffer_len < (header_len + footer_len + 1)) {
        return PROTOCOL_PARSER_ERROR_BUFFER_TOO_SMALL;
    }

    // 如果已初始化，先反初始化
    if (ctx->initialized) {
        (void)protocol_parser_deinit(ctx);
    }

    // 保存配置
    ctx->config = *config;
    ctx->config.header_len = header_len;
    ctx->config.footer_len = footer_len;
    ctx->header_matched = false;
    ctx->last_log_time = 0;

    // 初始化静态FIFO缓冲区
    kfifo_init(&ctx->fifo, config->input_buffer, config->input_buffer_len, NULL);

    // 清空状态
    (void)protocol_parser_clear(ctx);
    ctx->initialized = true;

    return PROTOCOL_PARSER_OK;
}

/**
 * @brief 反初始化协议解析器
 * @param ctx 协议解析器上下文指针
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_deinit(protocol_parser_context_t* ctx)
{
    if (!ctx) {
        return PROTOCOL_PARSER_ERROR_NULL_PTR;
    }

    kfifo_reset(&ctx->fifo);

    (void)protocol_parser_clear(ctx);
    ctx->initialized = false;

    return PROTOCOL_PARSER_OK;
}

/**
 * @brief 检查解析器是否已初始化
 * @param ctx 协议解析器上下文指针
 * @return 1表示已初始化，0表示未初始化
 */
bool protocol_parser_is_initialized(const protocol_parser_context_t* ctx)
{
    return (ctx && ctx->initialized);
}

/**
 * @brief 向解析器输入数据
 * @param ctx 协议解析器上下文指针
 * @param data 输入数据指针
 * @param len 数据长度
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_feed(protocol_parser_context_t* ctx,
    const uint8_t* data,
    uint32_t len)
{
    // 检查参数有效性
    if (!ctx || !data || len == 0 || !ctx->fifo.buffer || !ctx->initialized) {
        return PROTOCOL_PARSER_ERROR_INVALID_PARAM;
    }

    // 重置空闲计时器
    ctx->idle_timer = 0;

    // 写入FIFO
    uint32_t put_len = kfifo_put(&ctx->fifo, (void*)data, len);
    if (put_len != len) {
        kfifo_reset(&ctx->fifo);
        return PROTOCOL_PARSER_ERROR_BUFFER_OVERFLOW;
    }

    return PROTOCOL_PARSER_OK;
}

/**
 * @brief 清空解析器缓冲区
 * @param ctx 协议解析器上下文指针
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_clear(protocol_parser_context_t* ctx)
{
    if (!ctx) {
        return PROTOCOL_PARSER_ERROR_NULL_PTR;
    }

    if (ctx->fifo.buffer) {
        kfifo_reset(&ctx->fifo);
    }

    ctx->header_matched = false;
    ctx->idle_timer = 0;
    ctx->last_log_time = 0;

    return PROTOCOL_PARSER_OK;
}

/**
 * @brief 更新空闲计时器
 * @param ctx 协议解析器上下文指针
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_tick(protocol_parser_context_t* ctx)
{
    if (!ctx) {
        return PROTOCOL_PARSER_ERROR_NULL_PTR;
    }

    ctx->idle_timer++;
    ctx->tick_count++;

    return PROTOCOL_PARSER_OK;
}

/**
 * @brief 解析并获取完整帧
 * @param ctx 协议解析器上下文指针
 * @param len 输出参数，返回帧长度
 * @param p_out_data 输出参数，返回帧数据指针
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_parse(protocol_parser_context_t* ctx,
    uint16_t* len,
    uint8_t** p_out_data)
{
    // 检查参数有效性
    if (!ctx || !len || !p_out_data || !ctx->fifo.buffer || !ctx->initialized) {
        return PROTOCOL_PARSER_ERROR_INVALID_PARAM;
    }

    // 初始化输出参数
    *p_out_data = NULL;
    *len = 0;

    uint16_t i, j;
    uint16_t payload_len;

    // 检查帧头
    if (!ctx->header_matched) {
        const protocol_parser_error_t err = parser_check_header(ctx);
        if (err == PROTOCOL_PARSER_ERROR_INCOMPLETE) {
            return PROTOCOL_PARSER_ERROR_INCOMPLETE;
        }
        if (err != PROTOCOL_PARSER_OK) {
            return err;
        }
        if (ctx->config.header_len != 0) {
            memcpy(ctx->config.output_buffer, ctx->config.header,
                ctx->config.header_len);
        }
        ctx->header_matched = true;
    }

    // 获取帧长度
    if (ctx->config.get_len_cb == NULL) {
        return PROTOCOL_PARSER_ERROR_CALLBACK_NULL;
    }
    uint16_t remaining_buffer = (ctx->config.output_buffer_len > ctx->config.header_len)
        ? (ctx->config.output_buffer_len - ctx->config.header_len)
        : 0;

    uint16_t data_len = kfifo_peek(
        &ctx->fifo, &ctx->config.output_buffer[ctx->config.header_len],
        remaining_buffer, 0);

    if (data_len > 0 && data_len <= remaining_buffer) {
        payload_len = ctx->config.get_len_cb(ctx->config.output_buffer,
            data_len + ctx->config.header_len);
    } else {
        payload_len = 0;
    }

    // 减去帧头长度
    if (payload_len >= ctx->config.header_len) {
        payload_len = payload_len - ctx->config.header_len;
    }

    // 检查缓冲区溢出
    if ((ctx->config.header_len + payload_len) > ctx->config.output_buffer_len) {
        ctx->header_matched = false;
        return PROTOCOL_PARSER_ERROR_BUFFER_OVERFLOW;
    }

    // 检查数据是否完整
    if ((data_len < payload_len) || (payload_len == 0)) {
        uint32_t idle_threshold = (uint32_t)(((uint64_t)IDLE_FRAME_FACTOR_NUMERATOR * (payload_len + 1)) / IDLE_FRAME_FACTOR_DENOMINATOR);

        if (ctx->idle_timer > idle_threshold) {
            ctx->idle_timer = 0;
            ctx->header_matched = false;
            /* 丢弃前导垃圾字节以防止无限循环:
             * 当 kfifo 中以非帧头字节开头时, header_len>0 的情况下
             * 不清除该字节会导致每次 parse 都反复匹配同一个垃圾字节 */
            kfifo_skip(&ctx->fifo, 1);
            return PROTOCOL_PARSER_ERROR_IDLE_TIMEOUT;
        }
        return PROTOCOL_PARSER_ERROR_INCOMPLETE;
    }

    uint16_t total_len = payload_len + ctx->config.header_len;

    // 检查帧长度有效性
    if (total_len < ctx->config.footer_len) {
        ctx->header_matched = false;
        return PROTOCOL_PARSER_ERROR_FRAME_INVALID;
    }

    // 检查帧尾
    i = total_len - ctx->config.footer_len;

    for (j = 0; j < ctx->config.footer_len;) {
        if (parser_safe_access(ctx->config.output_buffer, i,
                ctx->config.output_buffer_len)
            == parser_safe_access(ctx->config.footer, j, ctx->config.footer_len)) {
            i++;
            j++;
        } else {
            break;
        }
    }

    // 帧尾匹配成功
    if (j == ctx->config.footer_len) {
        *len = ctx->config.header_len + payload_len;
        if (ctx->config.check_cb == NULL) {
            // 无校验回调，直接返回成功
            ctx->header_matched = false;
            ctx->last_log_time = 0;
            kfifo_skip(&ctx->fifo, payload_len);
            *p_out_data = ctx->config.output_buffer;
            return PROTOCOL_PARSER_OK;
        }
        const protocol_parser_error_t err = ctx->config.check_cb(ctx->config.output_buffer, *len);
        if (err == PROTOCOL_PARSER_OK) {
            ctx->header_matched = false;
            ctx->last_log_time = 0;
            kfifo_skip(&ctx->fifo, payload_len);
            *p_out_data = ctx->config.output_buffer;
            return PROTOCOL_PARSER_OK;
        }
        if (ctx->config.header_len == 0) {
            kfifo_skip(&ctx->fifo, 1);
        }
        return PROTOCOL_PARSER_ERROR_CHECKSUM;
    }

    // 帧尾不匹配
    ctx->header_matched = false;
    ctx->last_log_time = 0;
    return PROTOCOL_PARSER_ERROR_FOOTER_MISMATCH;
}
