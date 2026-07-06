//
// Created by fubingyan on 2026-06-21.
//

#include "msg_fifo.h"

bool msg_fifo_init(msg_fifo_t* q, void* buffer, uint32_t buffer_size,
    uint16_t element_size)
{
    if (!q || !buffer || element_size == 0 || buffer_size == 0) {
        return false;
    }

    // 缓冲区至少要能容纳一个完整元素
    if (buffer_size < element_size) {
        return false;
    }

    q->element_size = element_size;

    // kfifo 要求 size 为 2 的幂；向下取整（取最接近的 2 的幂且 ≤ buffer_size）
    // 避免 kfifo 内部 roundup_pow_of_two 向上取整导致越界写
    uint32_t kfifo_size = 1;
    while (kfifo_size <= (buffer_size >> 1)) {
        kfifo_size <<= 1;
    }

    kfifo_init(&q->fifo, (unsigned char*)buffer, (unsigned int)kfifo_size, NULL);

    return true;
}

bool msg_fifo_push(msg_fifo_t* q, const void* element)
{
    if (!q || !element || q->element_size == 0) {
        return false;
    }

    // 1. 核心改进：写入前检查剩余空间是否足够存放一个【完整元素】
    // 若移植的 kfifo 没有定义 kfifo_avail，可替换为: (kfifo_size(&q->fifo) - kfifo_len(&q->fifo))
    if (kfifo_avail(&q->fifo) < q->element_size) {
        return false;
    }

    // 2. 核心改进：确保实际写入的数据大小与元素大小完全一致
    return kfifo_put(&q->fifo, (const unsigned char*)element,
               (unsigned int)q->element_size)
        == q->element_size;
}

bool msg_fifo_pop(msg_fifo_t* q, void* element)
{
    if (!q || !element || q->element_size == 0) {
        return false;
    }

    // 1. 核心改进：读取前检查已存字节是否足够读出一个【完整元素】
    if (kfifo_len(&q->fifo) < q->element_size) {
        return false;
    }

    // 2. 核心改进：确保实际读取的数据大小与元素大小完全一致
    return kfifo_get(&q->fifo, (unsigned char*)element,
               (unsigned int)q->element_size)
        == q->element_size;
}

bool msg_fifo_empty(const msg_fifo_t* q)
{
    if (!q) {
        return true; // 空指针视为空队列
    }
    // 注意：若底层 kfifo_is_empty 接受 const 结构指针，可去除强转
    return kfifo_is_empty((kfifo_t*)&q->fifo);
}

void msg_fifo_deinit(msg_fifo_t* q)
{
    if (!q) return;
    kfifo_reset(&q->fifo);
    q->element_size = 0;
}