/**
 * @brief: 内核 FIFO 头文件（平台无关 & 扁平结构）
 * @FilePath: kfifo.h
 * @author: maximilian
 * @version: V1.0.0
 * @note: Linux 内核风格的环形缓冲区实现，已适配 HPM RISC-V MCU 平台
 * @copyright (c) 2025 by maximilian, All Rights Reserved.
 */

#ifndef KFIFO_H
#define KFIFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================== */
/*                    【平台移植接口层 — HPM RISC-V MCU】                       */
/* ========================================================================== */

/* 1. 引入平台底层依赖头文件 */
#include "main.h"
/* 2. 内存分配与释放接口 — 本项目使用静态内存分配，禁用动态分配 */
#ifndef KFIFO_MALLOC
#define KFIFO_MALLOC(size) NULL /* 静态分配模式，返回 NULL */
#endif

#ifndef KFIFO_FREE
#define KFIFO_FREE(ptr) ((void)(ptr))
#endif

/* 3. 数据内存屏障接口
 * RISC-V 架构使用 fence 指令确保 DMA 写入对 CPU 可见。
 * HPM6E80 的 DMA 缓冲区已放置在 non-cacheable 区域，但 fence 保证顺序。 */
#ifndef KFIFO_MEM_BARRIER
#define KFIFO_MEM_BARRIER()
#endif

/* 4. 临界区保护接口（解决多线程/嵌套中断并发安全）
 * RISC-V 通过 mstatus.MIE 位控制全局中断，将 MIE 状态压栈/出栈实现嵌套保护。 */
typedef uint32_t kfifo_lock_state_t;

#ifndef KFIFO_LOCK
/* STM32 裸机安全嵌套中断锁。如果用 FreeRTOS，可换成 taskENTER_CRITICAL_FROM_ISR
 */
#define KFIFO_LOCK(flags)          \
    do {                           \
        (flags) = __get_PRIMASK(); \
        __disable_irq();           \
    } while (0)
#endif

#ifndef KFIFO_UNLOCK
#define KFIFO_UNLOCK(flags)   \
    do {                      \
        __set_PRIMASK(flags); \
    } while (0)
#endif


/* ==========================================================================
 * 以上为平台移植区，HPM MCU 平台已完成适配
 * 以下为内核实现区，无需修改
 * ========================================================================== */

/**
 * @brief 高级运行状态枚举（细化水位，方便硬件流控与报警）
 */
typedef enum {
    /* --- 错误状态区 (<0) --- */
    KFIFO_ERR_NULL = -1, /**< 传入了空指针 */
    KFIFO_ERR_UNINIT = -2, /**< 缓冲区未初始化 */

    /* --- 正常运行状态区 (>=0) --- */
    KFIFO_EMPTY = 0, /**< 队列全空：无数据可读 */
    KFIFO_ALMOST_EMPTY = 1, /**< 将空水位：数据量不足 1/4 */
    KFIFO_PARTIAL = 2, /**< 常规状态：正常部分填充 */
    KFIFO_HALFFULL = 3, /**< 半满状态：数据量在 37.5% ~ 62.5% */
    KFIFO_ALMOST_FULL = 4, /**< 将满水位：数据量超过 3/4（需警惕溢出） */
    KFIFO_FULL = 5 /**< 队列全满：无法再写入 */
} kfifo_state;

/**
 * @brief kfifo 结构体
 */
typedef struct {
    unsigned char* buffer; /**< 缓冲区指针 */
    unsigned int size; /**< 缓冲区大小（必须为 2 的幂） */
    unsigned int in; /**< 写入位置（生产者指针） */
    unsigned int out; /**< 读取位置（消费者指针） */
    uint32_t* lock; /**< 锁指针，用于线程安全（NULL 表示无锁保护） */
} kfifo_t;

/* --- 函数声明 --- */

/**
 * @brief 初始化 kfifo 结构体（静态分配内存）
 * @param fifo   kfifo 结构体指针
 * @param buffer 缓冲区指针
 * @param size   缓冲区大小，必须为 2 的幂
 * @param lock   锁指针，用于线程安全（传入 NULL 则不使用锁）
 */
void kfifo_init(kfifo_t* fifo, unsigned char* buffer, unsigned int size,
    uint32_t* lock);

/**
 * @brief 向 kfifo 放入数据
 * @param fifo   kfifo 指针
 * @param buffer 数据缓冲区
 * @param len    数据长度
 * @return 实际放入的数据长度
 */
unsigned int kfifo_put(kfifo_t* fifo, const unsigned char* buffer,
    unsigned int len);

/**
 * @brief 从 kfifo 取出数据
 * @param fifo   kfifo 指针
 * @param buffer 数据缓冲区
 * @param len    数据长度
 * @return 实际取出的数据长度
 */
unsigned int kfifo_get(kfifo_t* fifo, unsigned char* buffer, unsigned int len);

/**
 * @brief 查看环形缓冲区中的数据而不移除
 * @param fifo   kfifo 指针
 * @param buffer 数据缓冲区
 * @param len    期望查看的数据长度
 * @param offset 查看偏移量（从当前读取位置开始）
 * @return 实际查看的数据长度
 */
uint32_t kfifo_peek(kfifo_t* fifo, uint8_t* buffer, uint32_t len,
    uint32_t offset);

/**
 * @brief 跳过指定长度的数据而不读取
 * @param fifo kfifo 指针
 * @param len  要跳过的数据长度
 * @return 实际跳过的数据长度
 */
uint32_t kfifo_skip(kfifo_t* fifo, uint32_t len);

/**
 * @brief 获取 kfifo 中数据长度
 * @param fifo kfifo 指针
 * @return 数据长度
 */
unsigned int kfifo_len(kfifo_t* fifo);

/**
 * @brief 获取 kfifo 中可用空间
 * @param fifo kfifo 指针
 * @return 可写入的数据长度
 */
unsigned int kfifo_avail(kfifo_t* fifo);

/**
 * @brief 重置 kfifo
 * @param fifo kfifo 指针
 */
void kfifo_reset(kfifo_t* fifo);

/**
 * @brief 清除环形缓冲区中指定长度的数据
 * @param fifo kfifo 指针
 * @param len  要清除的数据长度
 * @return 实际清除的数据长度
 */
unsigned int kfifo_clear_len(kfifo_t* fifo, unsigned int len);

/**
 * @brief 同步 DMA 硬件写入位置到 kfifo（DMA CIRCLE 模式专用）
 * @param fifo         kfifo 指针
 * @param dma_hw_index 当前 DMA 硬件指向的物理数组下标 (0 到 size-1)
 * @note  通过 (fifo->size - dma_get_remaining_transfer_size(...)) 计算得到
 */
void kfifo_move_in(kfifo_t* fifo, unsigned int dma_hw_index);

/**
 * @brief 前移 kfifo 写入指针（DMA NORMAL 模式专用）
 * @param fifo kfifo 指针
 * @param len  移动长度
 * @note  外部（如 DMA）已将数据写入缓冲区，调用此函数通知 FIFO 有新数据
 */
void kfifo_skip_in(kfifo_t* fifo, const unsigned int len);

/**
 * @brief 获取 kfifo 状态
 * @param fifo kfifo 指针
 * @return kfifo 状态枚举值
 */
kfifo_state kfifo_status(kfifo_t* fifo);

/**
 * @brief 获取 kfifo 的总大小
 * @param fifo kfifo 指针
 * @return 总大小
 */
unsigned int kfifo_size(kfifo_t* fifo);

/**
 * @brief 检查 kfifo 是否为空
 * @param fifo kfifo 指针
 * @return 1 为空，0 为非空
 */
int kfifo_is_empty(kfifo_t* fifo);

/**
 * @brief 检查 kfifo 是否为满
 * @param fifo kfifo 指针
 * @return 1 为满，0 为不满
 */
int kfifo_is_full(kfifo_t* fifo);

/* --- 宏定义 --- */

/**
 * @brief 静态初始化 kfifo
 */
#define KFIFO_INIT(name, buffer, size) { (buffer), (size), 0, 0, NULL }

/**
 * @brief 检查 kfifo 是否已初始化
 */
#define kfifo_initialized(fifo) ((fifo)->buffer != NULL)

#ifdef __cplusplus
}
#endif

#endif /* KFIFO_H */
