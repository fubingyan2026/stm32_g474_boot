//
// Created by fubingyan on 2026-06-21.
//

#ifndef __EVENT_H
#define __EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 事件标志结构体（4 字节，ISR 安全）
 */
typedef struct {
    volatile uint32_t flags; /**< 事件标志位 */
} event_t;

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化事件标志
 * @param e 事件指针
 */
void event_init(event_t* e);

/**
 * @brief 发送事件（ISR 安全）
 * @param e 事件指针
 * @param flags 要置位的标志
 */
void event_send(event_t* e, uint32_t flags);

/**
 * @brief 接收事件（主循环轮询）
 * @param e 事件指针
 * @param mask 感兴趣的事件掩码
 * @param all true=等待所有标志，false=任一标志
 * @param clear true=接收后清除
 * @return true 表示事件满足条件
 */
bool event_recv(event_t* e, uint32_t mask, bool all, bool clear);

#ifdef __cplusplus
}
#endif

#endif /* __EVENT_H */
