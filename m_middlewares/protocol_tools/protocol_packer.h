
#ifndef __PROTOCOL_PACKER_H
#define __PROTOCOL_PACKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 协议打包器错误码枚举
 */
typedef enum {
  PROTOCOL_PACKER_OK = 0,                 /**< 操作成功 */
  PROTOCOL_PACKER_ERROR_NULL_PTR,         /**< 空指针错误 */
  PROTOCOL_PACKER_ERROR_INVALID_PARAM,    /**< 无效参数 */
  PROTOCOL_PACKER_ERROR_UNINITIALIZED,    /**< 未初始化 */
  PROTOCOL_PACKER_ERROR_BUFFER_OVERFLOW,  /**< 缓冲区溢出 */
  PROTOCOL_PACKER_ERROR_BUFFER_TOO_SMALL, /**< 缓冲区过小 */
  PROTOCOL_PACKER_ERROR_CALLBACK_NULL,    /**< 回调函数为空 */
  PROTOCOL_PACKER_ERROR_GENERIC,          /**< 通用错误 */
} protocol_packer_error_t;

/**
 * @brief 校验码计算回调函数类型
 * @param data 输入数据缓冲区
 * @param len 数据长度
 * @param checksum_out 输出校验码缓冲区
 * @param checksum_len 输出校验码长度
 * @return 校验计算结果错误码
 */
typedef protocol_packer_error_t (*protocol_packer_checksum_cb_t)(
    const uint8_t* data, uint16_t len, uint8_t* checksum_out,
    uint16_t* checksum_len);

/**
 * @brief 帧长度字段填充回调函数类型
 * @param buffer 帧缓冲区（包含帧头）
 * @param payload_len 负载数据长度
 * @return 填充结果错误码
 */
typedef protocol_packer_error_t (*protocol_packer_fill_len_cb_t)(
    uint8_t* buffer, uint16_t payload_len);

/**
 * @brief 协议打包器配置结构体
 */
typedef struct {
  const char* name;                          /**< 协议名称 */
  const uint8_t* header;                     /**< 帧头数据 */
  const uint8_t* footer;                     /**< 帧尾数据 */
  uint8_t* output_buffer;                    /**< 输出帧缓冲区 */
  protocol_packer_checksum_cb_t checksum_cb; /**< 校验码计算回调 */
  protocol_packer_fill_len_cb_t fill_len_cb; /**< 帧长度填充回调 */
  uint16_t header_len;                       /**< 帧头长度 */
  uint16_t footer_len;                       /**< 帧尾长度 */
  uint16_t checksum_len;                     /**< 校验码长度 */
  uint16_t output_buffer_len;                /**< 输出缓冲区大小 */
} protocol_packer_config_t;

/**
 * @brief 协议打包器上下文结构体前向声明
 */
typedef struct protocol_packer_context protocol_packer_context_t;

/**
 * @brief 协议打包器上下文结构体
 *
 * 用于保存协议打包器实例的状态信息，支持多实例操作。
 */
struct protocol_packer_context {
  protocol_packer_config_t config; /**< 配置参数 */
  uint16_t last_frame_len;         /**< 最近打包帧长度 */
  bool initialized;                /**< 初始化标志 */
};

/* Exported constants --------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化协议打包器
 * @param ctx 协议打包器上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
protocol_packer_error_t protocol_packer_init(
    protocol_packer_context_t* ctx, const protocol_packer_config_t* config);

/**
 * @brief 反初始化协议打包器
 * @param ctx 协议打包器上下文指针
 * @return 操作结果错误码
 */
protocol_packer_error_t protocol_packer_deinit(protocol_packer_context_t* ctx);

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
                                             uint16_t* p_out_len);

/**
 * @brief 获取最近打包的帧数据
 * @param ctx 协议打包器上下文指针
 * @param p_out_frame 输出参数，返回帧数据指针
 * @param p_out_len 输出参数，返回帧长度
 * @return 操作结果错误码
 */
protocol_packer_error_t protocol_packer_get_frame(
    protocol_packer_context_t* ctx, uint8_t** p_out_frame, uint16_t* p_out_len);

/**
 * @brief 清空输出缓冲区
 * @param ctx 协议打包器上下文指针
 * @return 操作结果错误码
 */
protocol_packer_error_t protocol_packer_clear(protocol_packer_context_t* ctx);

/**
 * @brief 检查打包器是否已初始化
 * @param ctx 协议打包器上下文指针
 * @return true表示已初始化，false表示未初始化
 */
bool protocol_packer_is_initialized(const protocol_packer_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOL_PACKER_H */
