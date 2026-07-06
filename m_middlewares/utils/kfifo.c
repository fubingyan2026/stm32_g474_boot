/**
 * @brief: 内核 FIFO 实现（平台无关 & 扁平结构）
 * @FilePath: kfifo.c
 * @author: maximilian
 * @version: V1.0.0
 * @note: Linux 内核风格的环形缓冲区实现，已适配 HPM RISC-V MCU 平台
 * @copyright (c) 2026 by maximilian, All Rights Reserved.
 */

#include "kfifo.h"

/**
 * @brief 判断一个数是否为 2 的幂次方
 * @param n 要判断的数
 * @return 1 表示是 2 的幂，0 表示不是
 */
static inline int is_power_of_2(unsigned int n)
{
    return (n != 0) && ((n & (n - 1)) == 0);
}

/**
 * @brief 将一个数向上取整到最近的 2 的幂次方
 * @param n 输入数
 * @return 最近的 2 的幂次方数
 */
static inline unsigned int roundup_pow_of_two(unsigned int n)
{
    if (n == 0)
        return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

/**
 * @brief 取两个数中的最小值
 */
static inline unsigned int min(unsigned int a, unsigned int b)
{
    return a < b ? a : b;
}

void kfifo_init(kfifo_t* fifo, unsigned char* buffer, unsigned int size, uint32_t* lock)
{
    if (fifo == NULL || buffer == NULL)
        return;

    if (!is_power_of_2(size)) {
        size = roundup_pow_of_two(size);
    }

    fifo->buffer = buffer;
    fifo->size = size;
    fifo->in = fifo->out = 0;
    fifo->lock = lock;
}

unsigned int kfifo_put(kfifo_t* fifo, const unsigned char* buffer, unsigned int len)
{
    if (fifo == NULL || buffer == NULL || len == 0)
        return 0;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);

    len = min(len, fifo->size - (fifo->in - fifo->out));
    if (len > 0) {
        const unsigned int l = min(len, fifo->size - (fifo->in & (fifo->size - 1)));
        memcpy(fifo->buffer + (fifo->in & (fifo->size - 1)), buffer, l);

        /* 避免无效拷贝 */
        if (len > l) {
            memcpy(fifo->buffer, buffer + l, len - l);
        }

        /* 平台隔离层：确保数据对 DMA/多核可见 */
        KFIFO_MEM_BARRIER();
        fifo->in += len;
    }

    KFIFO_UNLOCK(flags);
    return len;
}

unsigned int kfifo_get(kfifo_t* fifo, unsigned char* buffer, unsigned int len)
{
    if (fifo == NULL || buffer == NULL || len == 0)
        return 0;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);

    len = min(len, fifo->in - fifo->out);
    if (len > 0) {
        const unsigned int l = min(len, fifo->size - (fifo->out & (fifo->size - 1)));
        memcpy(buffer, fifo->buffer + (fifo->out & (fifo->size - 1)), l);

        /* 避免无效拷贝 */
        if (len > l) {
            memcpy(buffer + l, fifo->buffer, len - l);
        }

        /* 平台隔离层：确保读取完毕后再更新 out 指针 */
        KFIFO_MEM_BARRIER();

        /* 保持自然溢出机制，只推进 out，不重置 out = in */
        fifo->out += len;
    }

    KFIFO_UNLOCK(flags);
    return len;
}

uint32_t kfifo_skip(kfifo_t* fifo, uint32_t len)
{
    if (fifo == NULL || len == 0)
        return 0;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);

    len = min(len, fifo->in - fifo->out);
    if (len > 0) {
        /* 仅推进 out 指针跳过数据，不重置指针 */
        fifo->out += len;
    }

    KFIFO_UNLOCK(flags);
    return len;
}

uint32_t kfifo_peek(kfifo_t* fifo, uint8_t* buffer, uint32_t len, uint32_t offset)
{
    if (fifo == NULL || buffer == NULL || len == 0)
        return 0;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);

    const uint32_t available = fifo->in - fifo->out;
    if (offset >= available) {
        KFIFO_UNLOCK(flags);
        return 0;
    }

    len = min(len, available - offset);
    const uint32_t out = fifo->out + offset;

    const uint32_t l = min(len, fifo->size - (out & (fifo->size - 1)));
    memcpy(buffer, fifo->buffer + (out & (fifo->size - 1)), l);

    if (len > l) {
        memcpy(buffer + l, fifo->buffer, len - l);
    }

    KFIFO_UNLOCK(flags);
    return len;
}

unsigned int kfifo_len(kfifo_t* fifo)
{
    if (fifo == NULL)
        return 0;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);
    const unsigned int len = fifo->in - fifo->out;
    KFIFO_UNLOCK(flags);

    return len;
}

unsigned int kfifo_avail(kfifo_t* fifo)
{
    if (fifo == NULL)
        return 0;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);
    const unsigned int avail = fifo->size - (fifo->in - fifo->out);
    KFIFO_UNLOCK(flags);

    return avail;
}

void kfifo_reset(kfifo_t* fifo)
{
    if (fifo == NULL)
        return;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);
    fifo->in = fifo->out = 0;
    KFIFO_UNLOCK(flags);
}

unsigned int kfifo_clear_len(kfifo_t* fifo, unsigned int len)
{
    if (fifo == NULL || len == 0)
        return 0;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);

    len = min(len, fifo->in - fifo->out);
    fifo->out += len;

    KFIFO_UNLOCK(flags);
    return len;
}

void kfifo_skip_in(kfifo_t* fifo, const unsigned int len)
{
    if (fifo == NULL || len == 0)
        return;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);

    unsigned int available = fifo->size - (fifo->in - fifo->out);
    unsigned int actual_len = min(len, available);

    /* 平台隔离层：确保 DMA 数据真正到达内存，再更新软件 in 指针 */
    KFIFO_MEM_BARRIER();
    fifo->in += actual_len;

    KFIFO_UNLOCK(flags);
}

void kfifo_move_in(kfifo_t* fifo, unsigned int dma_hw_index)
{
    if (fifo == NULL)
        return;

    dma_hw_index = dma_hw_index & (fifo->size - 1);

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);

    const unsigned int current_in = fifo->in & (fifo->size - 1);
    const unsigned int move_in_len = (dma_hw_index >= current_in)
        ? (dma_hw_index - current_in)
        : (dma_hw_index + fifo->size - current_in);

    if (move_in_len > 0) {
        /* 内存隔离层：确保 DMA 数据真正到达 RAM，再更新软件 in 指针 */
        KFIFO_MEM_BARRIER();
        fifo->in += move_in_len;

        if ((fifo->in - fifo->out) > fifo->size) {
            fifo->out = fifo->in - fifo->size;
        }
    }

    KFIFO_UNLOCK(flags);
}

kfifo_state kfifo_status(kfifo_t* fifo)
{
    if (fifo == NULL)
        return KFIFO_ERR_NULL;
    if (fifo->buffer == NULL || fifo->size == 0)
        return KFIFO_ERR_UNINIT;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);
    const unsigned int len = fifo->in - fifo->out;
    KFIFO_UNLOCK(flags);

    if (len == 0) {
        return KFIFO_EMPTY;
    } else if (len == fifo->size) {
        return KFIFO_FULL;
    } else if (len >= (fifo->size - (fifo->size >> 2))) {
        /* 队列数据量 >= 75% (即将溢出警戒) */
        return KFIFO_ALMOST_FULL;
    } else if (len >= ((fifo->size >> 1) - (fifo->size >> 3))
        && len <= ((fifo->size >> 1) + (fifo->size >> 3))) {
        /* 队列数据量在 37.5% ~ 62.5% 之间 (半满状态) */
        return KFIFO_HALFFULL;
    } else if (len <= (fifo->size >> 2)) {
        /* 队列数据量 <= 25% (即将枯竭警戒) */
        return KFIFO_ALMOST_EMPTY;
    }

    return KFIFO_PARTIAL;
}

unsigned int kfifo_size(kfifo_t* fifo)
{
    return fifo ? fifo->size : 0;
}

int kfifo_is_empty(kfifo_t* fifo)
{
    if (fifo == NULL)
        return 1;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);
    int empty = (fifo->in == fifo->out);
    KFIFO_UNLOCK(flags);

    return empty;
}

int kfifo_is_full(kfifo_t* fifo)
{
    if (fifo == NULL)
        return 0;

    kfifo_lock_state_t flags;
    KFIFO_LOCK(flags);
    int full = (fifo->size - (fifo->in - fifo->out)) == 0;
    KFIFO_UNLOCK(flags);

    return full;
}
