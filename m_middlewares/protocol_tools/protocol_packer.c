/**
 * @file    protocol_packer.c
 * @author  maximilian
 * @version V1.0.0
 * @date    2026-06-02
 * @brief   通用协议打包器实现
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
#include "protocol_packer.h"

#include <string.h>

/* Private constants ---------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化协议打包器
 * @param ctx 协议打包器上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
protocol_packer_error_t protocol_packer_init(
    protocol_packer_context_t* ctx, const protocol_packer_config_t* config)
{
    // 检查参数有效性
    if (!ctx || !config) {
        return PROTOCOL_PACKER_ERROR_NULL_PTR;
    }

    if (!config->output_buffer) {
        return PROTOCOL_PACKER_ERROR_NULL_PTR;
    }

    // 处理空指针情况
    uint16_t header_len = config->header_len;
    uint16_t footer_len = config->footer_len;
    uint16_t checksum_len = config->checksum_len;

    if (config->header == NULL) {
        header_len = 0;
    }
    if (config->footer == NULL) {
        footer_len = 0;
    }

    // 检查缓冲区大小
    uint16_t min_buffer_len = header_len + footer_len + checksum_len + 1;
    if (config->output_buffer_len < min_buffer_len) {
        return PROTOCOL_PACKER_ERROR_BUFFER_TOO_SMALL;
    }

    // 如果已初始化，先反初始化
    if (ctx->initialized) {
        (void)protocol_packer_deinit(ctx);
    }

    // 保存配置
    ctx->config = *config;
    ctx->config.header_len = header_len;
    ctx->config.footer_len = footer_len;
    ctx->config.checksum_len = checksum_len;

    // 初始化运行时状态
    ctx->last_frame_len = 0;
    ctx->initialized = true;

    return PROTOCOL_PACKER_OK;
}

/**
 * @brief 反初始化协议打包器
 * @param ctx 协议打包器上下文指针
 * @return 操作结果错误码
 */
protocol_packer_error_t protocol_packer_deinit(protocol_packer_context_t* ctx)
{
    if (!ctx) {
        return PROTOCOL_PACKER_ERROR_NULL_PTR;
    }

    (void)protocol_packer_clear(ctx);
    ctx->initialized = false;

    return PROTOCOL_PACKER_OK;
}

/**
 * @brief 检查打包器是否已初始化
 * @param ctx 协议打包器上下文指针
 * @return true表示已初始化，false表示未初始化
 */
bool protocol_packer_is_initialized(const protocol_packer_context_t* ctx)
{
    return (ctx && ctx->initialized);
}

/**
 * @brief 打包数据为完整帧
 * @param ctx 协议打包器上下文指针
 * @param data 输入数据指针
 * @param len 数据长度
 * @param p_out_frame 输出参数，返回帧数据指针
 * @param p_out_len 输出参数，返回帧长度
 * @return 操作结果错误码
 * @note 帧结构: [帧头][数据][校验码][帧尾]
 */
protocol_packer_error_t protocol_packer_pack(protocol_packer_context_t* ctx,
    const uint8_t* data, uint16_t len,
    uint8_t** p_out_frame,
    uint16_t* p_out_len)
{
    // 检查参数有效性
    if (!ctx || !p_out_frame || !p_out_len) {
        return PROTOCOL_PACKER_ERROR_NULL_PTR;
    }

    // 初始化输出参数
    *p_out_frame = NULL;
    *p_out_len = 0;

    if (!ctx->initialized) {
        return PROTOCOL_PACKER_ERROR_UNINITIALIZED;
    }

    if (!ctx->config.output_buffer) {
        return PROTOCOL_PACKER_ERROR_NULL_PTR;
    }

    // 数据可以为空（仅帧头帧尾的帧）
    if (len > 0 && !data) {
        return PROTOCOL_PACKER_ERROR_NULL_PTR;
    }

    // 计算总帧长度
    uint32_t total_len = (uint32_t)ctx->config.header_len + len + ctx->config.checksum_len + ctx->config.footer_len;

    // 检查缓冲区溢出
    if (total_len > ctx->config.output_buffer_len) {
        return PROTOCOL_PACKER_ERROR_BUFFER_OVERFLOW;
    }

    uint16_t offset = 0;

    // 添加帧头
    if (ctx->config.header_len > 0 && ctx->config.header != NULL) {
        memcpy(ctx->config.output_buffer, ctx->config.header,
            ctx->config.header_len);
        offset = ctx->config.header_len;
    }

    // 添加数据
    if (len > 0 && data != NULL) {
        memcpy(&ctx->config.output_buffer[offset], data, len);
        offset += len;
    }

    // 调用帧长度填充回调（在添加校验码之前）
    if (ctx->config.fill_len_cb != NULL) {
        (void)ctx->config.fill_len_cb(ctx->config.output_buffer, len);
    }

    // 添加校验码
    if (ctx->config.checksum_len > 0 && ctx->config.checksum_cb != NULL) {
        uint16_t checksum_len_out = ctx->config.checksum_len;
        protocol_packer_error_t err = ctx->config.checksum_cb(
            ctx->config.output_buffer, offset, &ctx->config.output_buffer[offset],
            &checksum_len_out);
        if (err != PROTOCOL_PACKER_OK) {
            return err;
        }
        offset += checksum_len_out;
    }

    // 添加帧尾
    if (ctx->config.footer_len > 0 && ctx->config.footer != NULL) {
        memcpy(&ctx->config.output_buffer[offset], ctx->config.footer,
            ctx->config.footer_len);
        offset += ctx->config.footer_len;
    }

    // 保存帧信息
    ctx->last_frame_len = offset;
    *p_out_frame = ctx->config.output_buffer;
    *p_out_len = offset;

    return PROTOCOL_PACKER_OK;
}

/**
 * @brief 获取最近打包的帧数据
 * @param ctx 协议打包器上下文指针
 * @param p_out_frame 输出参数，返回帧数据指针
 * @param p_out_len 输出参数，返回帧长度
 * @return 操作结果错误码
 */
protocol_packer_error_t protocol_packer_get_frame(
    protocol_packer_context_t* ctx, uint8_t** p_out_frame,
    uint16_t* p_out_len)
{
    // 检查参数有效性
    if (!ctx || !p_out_frame || !p_out_len) {
        return PROTOCOL_PACKER_ERROR_NULL_PTR;
    }

    // 初始化输出参数
    *p_out_frame = NULL;
    *p_out_len = 0;

    if (!ctx->initialized) {
        return PROTOCOL_PACKER_ERROR_UNINITIALIZED;
    }

    if (!ctx->config.output_buffer) {
        return PROTOCOL_PACKER_ERROR_NULL_PTR;
    }

    if (ctx->last_frame_len == 0) {
        return PROTOCOL_PACKER_ERROR_INVALID_PARAM;
    }

    *p_out_frame = ctx->config.output_buffer;
    *p_out_len = ctx->last_frame_len;

    return PROTOCOL_PACKER_OK;
}

/**
 * @brief 清空输出缓冲区
 * @param ctx 协议打包器上下文指针
 * @return 操作结果错误码
 */
protocol_packer_error_t protocol_packer_clear(protocol_packer_context_t* ctx)
{
    if (!ctx) {
        return PROTOCOL_PACKER_ERROR_NULL_PTR;
    }

    ctx->last_frame_len = 0;

    if (ctx->config.output_buffer && ctx->config.output_buffer_len > 0) {
        memset(ctx->config.output_buffer, 0, ctx->config.output_buffer_len);
    }

    return PROTOCOL_PACKER_OK;
}

/* Private functions ---------------------------------------------------------*/
