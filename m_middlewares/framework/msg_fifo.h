//
// Created by fubingyan on 2026-06-21.
//

#ifndef __MSG_FIFO_H
#define __MSG_FIFO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "kfifo.h"

#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 消息 FIFO 结构体（基于 kfifo，静态内存分配）
 */
typedef struct {
    kfifo_t fifo;          /**< 底层 kfifo */
    uint16_t element_size; /**< 单个元素字节数 */
} msg_fifo_t;

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化消息 FIFO
 * @param q FIFO 指针
 * @param buffer 静态缓冲区（调用者分配）
 * @param buffer_size 缓冲区总字节数（不小于 element_size）
 * @param element_size 单个元素字节数
 * @return true 初始化成功，false 初始化失败（参数不合法）
 * @note  buffer_size 不必为 2 的幂，内部自动向下取整到最接近的 2 的幂。
 *        例如 buffer_size=200 时实际使用 128 字节。
 */
bool msg_fifo_init(msg_fifo_t* q, void* buffer, uint32_t buffer_size,
    uint16_t element_size);

/**
 * @brief 入队（ISR 安全，单生产者）
 * @param q FIFO 指针
 * @param element 元素数据指针
 * @return true 成功，false 队列满（或空间不足以容纳一个完整元素）
 */
bool msg_fifo_push(msg_fifo_t* q, const void* element);

/**
 * @brief 出队（主循环，单消费者）
 * @param q FIFO 指针
 * @param element 接收缓冲区
 * @return true 成功，false 队列空（或剩余数据不足一个完整元素）
 */
bool msg_fifo_pop(msg_fifo_t* q, void* element);

/**
 * @brief 判空
 * @param q FIFO 指针
 * @return true 队列为空
 */
bool msg_fifo_empty(const msg_fifo_t* q);

/** @brief 反初始化，清空队列并重置状态 */
void msg_fifo_deinit(msg_fifo_t* q);

#ifdef __cplusplus
}
#endif

#endif /* __MSG_FIFO_H */