
#ifndef __PROTOCOL_PARSER_H
#define __PROTOCOL_PARSER_H

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
 * @brief 协议解析器错误码枚举
 */
typedef enum {
    PROTOCOL_PARSER_OK = 0, /**< 操作成功 */
    PROTOCOL_PARSER_ERROR_NULL_PTR, /**< 空指针错误 */
    PROTOCOL_PARSER_ERROR_INVALID_PARAM, /**< 无效参数 */
    PROTOCOL_PARSER_ERROR_UNINITIALIZED, /**< 未初始化 */
    PROTOCOL_PARSER_ERROR_MEMORY_ALLOC, /**< 内存分配失败 */
    PROTOCOL_PARSER_ERROR_BUFFER_OVERFLOW, /**< 缓冲区溢出 */
    PROTOCOL_PARSER_ERROR_BUFFER_TOO_SMALL, /**< 缓冲区过小 */
    PROTOCOL_PARSER_ERROR_CALLBACK_NULL, /**< 回调函数为空 */
    PROTOCOL_PARSER_ERROR_HEADER_MISMATCH, /**< 帧头不匹配 */
    PROTOCOL_PARSER_ERROR_FOOTER_MISMATCH, /**< 帧尾不匹配 */
    PROTOCOL_PARSER_ERROR_CHECKSUM, /**< 校验失败 */
    PROTOCOL_PARSER_ERROR_INCOMPLETE, /**< 数据不完整 */
    PROTOCOL_PARSER_ERROR_IDLE_TIMEOUT, /**< 空闲超时 */
    PROTOCOL_PARSER_ERROR_FRAME_INVALID, /**< 帧无效 */
    PROTOCOL_PARSER_ERROR_GENERIC, /**< 通用错误 */
} protocol_parser_error_t;

/**
 * @brief 帧校验回调函数类型
 * @param buffer 帧数据缓冲区
 * @param len 帧数据长度
 * @return 校验结果错误码
 */
typedef protocol_parser_error_t (*protocol_parser_check_cb_t)(uint8_t* buffer,
    uint16_t len);

/**
 * @brief 帧长度计算回调函数类型
 * @param buffer 帧数据缓冲区
 * @param len 当前数据长度
 * @return 完整帧长度，0表示数据不完整
 */
typedef uint16_t (*protocol_parser_get_len_cb_t)(uint8_t* buffer, uint16_t len);

/**
 * @brief 协议解析器配置结构体
 */
typedef struct {
    const char* name; /**< 协议名称 */
    const uint8_t* header; /**< 帧头数据 */
    const uint8_t* footer; /**< 帧尾数据 */
    uint8_t* output_buffer; /**< 输出帧缓冲区 */
    uint8_t* input_buffer; /**< 输入缓冲区（由调用者静态分配） */
    protocol_parser_get_len_cb_t get_len_cb; /**< 帧长度计算回调 */
    protocol_parser_check_cb_t check_cb; /**< 帧校验回调 */
    uint16_t header_len; /**< 帧头长度 */
    uint16_t footer_len; /**< 帧尾长度 */
    uint16_t input_buffer_len; /**< 输入缓冲区大小 */
    uint16_t output_buffer_len; /**< 输出缓冲区大小 */
} protocol_parser_config_t;

/**
 * @brief 协议解析器上下文结构体前向声明
 */
typedef struct protocol_parser_context protocol_parser_context_t;

/**
 * @brief 协议解析器上下文结构体
 *
 * 用于保存协议解析器实例的状态信息，支持多实例操作。
 */
struct protocol_parser_context {
    protocol_parser_config_t config; /**< 配置参数 */

    kfifo_t fifo; /**< 内部FIFO缓冲区（静态嵌入） */
    uint32_t idle_timer; /**< 空闲计时器 */
    uint32_t tick_count; /**< 系统计时器 */
    uint32_t last_log_time; /**< 上次日志时间 */
    bool header_matched; /**< 帧头匹配标志 */
    bool initialized; /**< 初始化标志 */
};

/* Exported constants --------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化协议解析器
 * @param ctx 协议解析器上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_init(
    protocol_parser_context_t* ctx, const protocol_parser_config_t* config);

/**
 * @brief 反初始化协议解析器
 * @param ctx 协议解析器上下文指针
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_deinit(protocol_parser_context_t* ctx);

/**
 * @brief 向解析器输入数据
 * @param ctx 协议解析器上下文指针
 * @param data 输入数据指针
 * @param len 数据长度
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_feed(protocol_parser_context_t* ctx,
    const uint8_t* data, uint32_t len);

/**
 * @brief 解析并获取完整帧
 * @param ctx 协议解析器上下文指针
 * @param len 输出参数，返回帧长度
 * @param p_out_data 输出参数，返回帧数据指针
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_parse(protocol_parser_context_t* ctx,
    uint16_t* len,
    uint8_t** p_out_data);

/**
 * @brief 清空解析器缓冲区
 * @param ctx 协议解析器上下文指针
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_clear(protocol_parser_context_t* ctx);

/**
 * @brief 更新空闲计时器
 * @param ctx 协议解析器上下文指针
 * @return 操作结果错误码
 * @note 需要在定时器中断或主循环中周期性调用
 */
protocol_parser_error_t protocol_parser_tick(protocol_parser_context_t* ctx);

/**
 * @brief 检查解析器是否已初始化
 * @param ctx 协议解析器上下文指针
 * @return 1表示已初始化，0表示未初始化
 */
bool protocol_parser_is_initialized(const protocol_parser_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOL_PARSER_H */
