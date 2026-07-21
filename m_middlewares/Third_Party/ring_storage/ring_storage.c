/**
 * @file    ring_storage.c
 * @brief   环形缓冲区 Flash 参数存储模块 - 核心实现
 * @author  FOC Development Team
 * @version V1.0.0
 * @date    2026-07-21
 *
 * @par 帧布局（Flash 中紧凑存储，写入时对齐到 write_gran）
 *
 *          偏移    字段             大小    说明
 *          ----------------------------------------------------
 *            0     magic            4B      0x52535446 ("RSTF")
 *            4     version          4B      单调递增版本号
 *            8     frame_len        4B      帧总逻辑大小（含头尾）
 *           12     kv_count         2B      KV 条目数
 *           14     header_crc16     2B      帧头 CRC16（偏移 0~13）
 *           16     KV 数据区        N B     [klen(1)][key][vlen(2)][val]...
 *          16+N   data_crc32       4B      KV 数据区 CRC32
 *          20+N   commit_magic     4B      0x434F4D54 ("COMT") 提交点
 *
 * @par 断电保护
 *          commit_magic 是最后写入的字段。若写入中断：
 *          - commit_magic 未写入（0xFFFFFFFF）→ 帧不完整，跳过
 *          - commit_magic 已写入但 data_crc32 不匹配 → 数据损坏，跳过
 *          STM32G4 双字编程下，data_crc32 + commit_magic 共 8B 一次写入，原子性保证。
 */

/* Includes ------------------------------------------------------------------*/
#include "ring_storage.h"
#include "ring_storage_port.h"

#include <string.h>

#include "algorithm/crc.h"      /* get_CRC16_check_sum */
#include "debug.h"              /* DEBUG_LOGI/E */

/* Private constants ---------------------------------------------------------*/

/* 帧魔数 "RSTF" (Ring STorage Frame) */
#define RS_FRAME_MAGIC                 0x52535446u
/* 提交魔数 "COMT" (COMmiT) */
#define RS_COMMIT_MAGIC                0x434F4D54u
/* 闪存空白值 */
#define RS_FLASH_EMPTY                 0xFFFFFFFFu

/* 帧头大小（字节） */
#define RS_HEADER_SIZE                 16u
/* 帧尾大小（字节） */
#define RS_FOOTER_SIZE                 8u
/* 帧固定开销 */
#define RS_FRAME_OVERHEAD              (RS_HEADER_SIZE + RS_FOOTER_SIZE)

/* Private types -------------------------------------------------------------*/

/* 帧头结构（紧凑，16 字节） */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /**< 帧魔数 */
    uint32_t version;           /**< 版本号 */
    uint32_t frame_len;         /**< 帧总逻辑大小 */
    uint16_t kv_count;          /**< KV 条目数 */
    uint16_t header_crc16;      /**< 帧头 CRC16 */
} rs_header_t;

/* 帧尾结构（紧凑，8 字节） */
typedef struct __attribute__((packed)) {
    uint32_t data_crc32;        /**< 数据 CRC32 */
    uint32_t commit_magic;      /**< 提交魔数 */
} rs_footer_t;

/* Private macros ------------------------------------------------------------*/

/* 对齐宏：向上对齐 */
#define RS_ALIGN_UP(size, align)        (((size) + (align) - 1) & ~((align) - 1))
/* 写入颗粒度（字节） */
#define RS_WRITE_ALIGN(ctx)             ((ctx)->config.write_gran / 8)
/* 帧在 Flash 中的对齐后大小 */
#define RS_FRAME_FLASH_SIZE(ctx, len)   RS_ALIGN_UP((len), RS_WRITE_ALIGN(ctx))
/* 扇区数量 */
#define RS_SECTOR_NUM(ctx)              ((ctx)->config.area_size / (ctx)->config.sector_size)
/* 扇区结束地址 */
#define RS_SECTOR_END(ctx, sec_addr)    ((sec_addr) + (ctx)->config.sector_size)
/* 活动扇区写入地址 */
#define RS_WRITE_ADDR(ctx)              ((ctx)->active_sector_addr + (ctx)->write_offset)

/* Private variables ---------------------------------------------------------*/

/* CRC32 查找表（多项式 0xEDB88320，与 zlib/PNG 兼容） */
static const uint32_t s_crc32_table[256] = {
    0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau, 0x076dc419u, 0x706af48fu,
    0xe963a535u, 0x9e6495a3u, 0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u,
    0x09b64c2bu, 0x7eb17cbdu, 0xe7b82d07u, 0x90bf1d91u, 0x1db71064u, 0x6ab020f2u,
    0xf3b97148u, 0x84be41deu, 0x1adad47du, 0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u,
    0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu, 0x14015c4fu, 0x63066cd9u,
    0xfa0f3d63u, 0x8d080df5u, 0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u, 0xa2677172u,
    0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu, 0x35b5a8fau, 0x42b2986cu,
    0xdbbbc9d6u, 0xacbcf940u, 0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u,
    0x26d930acu, 0x51de003au, 0xc8d75180u, 0xbfd06116u, 0x21b4f4b5u, 0x56b3c423u,
    0xcfba9599u, 0xb8bda50fu, 0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u,
    0x2f6f7c87u, 0x58684c11u, 0xc1611dabu, 0xb6662d3du, 0x76dc4190u, 0x01db7106u,
    0x98d220bcu, 0xefd5102au, 0x71b18589u, 0x06b6b51fu, 0x9fbfe4a5u, 0xe8b8d433u,
    0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u, 0x7f6a0dbbu, 0x086d3d2du,
    0x91646c97u, 0xe6635c01u, 0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu,
    0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u, 0x65b0d9c6u, 0x12b7e950u,
    0x8bbeb8eau, 0xfcb9887cu, 0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u,
    0x4db26158u, 0x3ab551ceu, 0xa3bc0074u, 0xd4bb30e2u, 0x4adfa541u, 0x3dd895d7u,
    0xa4d1c46du, 0xd3d6f4fbu, 0x4369e96au, 0x346ed9fcu, 0xad678846u, 0xda60b8d0u,
    0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u, 0x5005713cu, 0x270241aau,
    0xbe0b1010u, 0xc90c2086u, 0x5768b525u, 0x206f85b3u, 0xb966d409u, 0xce61e49fu,
    0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u, 0x59b33d17u, 0x2eb40d81u,
    0xb7bd5c3bu, 0xc0ba6cadu, 0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au,
    0xead54739u, 0x9dd277afu, 0x04db2615u, 0x73dc1683u, 0xe3630b12u, 0x94643b84u,
    0x0d6d6a3eu, 0x7a6a5aa8u, 0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u,
    0xf00f9344u, 0x8708a3d2u, 0x1e01f268u, 0x6906c2feu, 0xf762575du, 0x806567cbu,
    0x196c3671u, 0x6e6b06e7u, 0xfed41b76u, 0x89d32be0u, 0x10da7a5au, 0x67dd4accu,
    0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u, 0xd6d6a3e8u, 0xa1d1937eu,
    0x38d8c2c4u, 0x4fdff252u, 0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
    0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u, 0xdf60efc3u, 0xa867df55u,
    0x316e8eefu, 0x4669be79u, 0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u,
    0xcc0c7795u, 0xbb0b4703u, 0x220216b9u, 0x5505262fu, 0xc5ba3bbeu, 0xb2bd0b28u,
    0x2bb45a92u, 0x5cb36a04u, 0xc2d7ffa7u, 0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du,
    0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au, 0x9c0906a9u, 0xeb0e363fu,
    0x72076785u, 0x05005713u, 0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu, 0x0cb61b38u,
    0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u, 0x86d3d2d4u, 0xf1d4e242u,
    0x68ddb3f8u, 0x1fda836eu, 0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u,
    0x88085ae6u, 0xff0f6a70u, 0x66063bcau, 0x11010b5cu, 0x8f659effu, 0xf862ae69u,
    0x616bffd3u, 0x166ccf45u, 0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u,
    0xa7672661u, 0xd06016f7u, 0x4969474du, 0x3e6e77dbu, 0xaed16a4au, 0xd9d65adcu,
    0x40df0b66u, 0x37d83bf0u, 0xa9bcae53u, 0xdebb9ec5u, 0x47b2cf7fu, 0x30b5ffe9u,
    0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u, 0xbad03605u, 0xcdd70693u,
    0x54de5729u, 0x23d967bfu, 0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u,
    0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du,
};

/* Private function prototypes -----------------------------------------------*/

static uint32_t rs_calc_crc32(const void* data, size_t len);
static ring_storage_error_t rs_align_write(ring_storage_context_t* ctx, uint32_t addr,
                                           const uint8_t* data, size_t len);
static bool rs_is_sector_empty(ring_storage_context_t* ctx, uint32_t sec_addr);
static ring_storage_error_t rs_scan_for_latest_frame(ring_storage_context_t* ctx);
static ring_storage_error_t rs_gc_collect(ring_storage_context_t* ctx);
static size_t rs_serialize_kv(ring_storage_context_t* ctx, uint8_t* buf, size_t buf_size);
static ring_storage_error_t rs_parse_and_load_kv(ring_storage_context_t* ctx,
                                                  const uint8_t* data, size_t data_len,
                                                  uint16_t kv_count);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief   计算 CRC32（多项式 0xEDB88320）
 * @param   data 数据指针
 * @param   len  数据长度
 * @return  CRC32 值
 */
static uint32_t rs_calc_crc32(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;

    while (len--) {
        crc = s_crc32_table[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFFu;
}

/**
 * @brief   对齐写入 Flash
 * @param   ctx   上下文指针
 * @param   addr  Flash 地址
 * @param   data  数据指针
 * @param   len   数据长度
 * @return  操作结果错误码
 * @note    按 write_gran 对齐写入，末尾不足部分用 0xFF 填充。
 *          适用于 STM32G4 双字编程及更大颗粒度 Flash。
 *          注意：data 可能指向 ctx->config.frame_buffer，处理时避免源目标重叠。
 */
static ring_storage_error_t rs_align_write(ring_storage_context_t* ctx, uint32_t addr,
                                           const uint8_t* data, size_t len) {
    const uint32_t align = RS_WRITE_ALIGN(ctx);
    const size_t aligned_len = RS_ALIGN_UP(len, align);

    /* 长度已对齐，直接写入 */
    if (aligned_len == len) {
        return (ring_storage_error_t)ring_storage_port_write(addr, data, len);
    }

    /* 末尾需要对齐填充 */
    const size_t head = len - (len % align); /* 已对齐部分长度 */

    /* 先写入已对齐部分 */
    if (head > 0) {
        ring_storage_error_t err = (ring_storage_error_t)ring_storage_port_write(addr, data, head);
        if (err != RING_STORAGE_OK) {
            return err;
        }
    }

    /* 末尾不足一个对齐单元，用栈上临时缓冲区填充 0xFF 后写入 */
    uint8_t tail[32]; /* 最大支持 256bit = 32B */
    if (align > sizeof(tail)) {
        return RING_STORAGE_ERROR_INVALID_PARAM;
    }
    memset(tail, 0xFF, align);
    memcpy(tail, data + head, len - head);
    return (ring_storage_error_t)ring_storage_port_write(addr + head, tail, align);
}

/**
 * @brief   检查扇区是否为空（全 0xFF）
 * @param   ctx       上下文指针
 * @param   sec_addr  扇区起始地址
 * @return  true 扇区为空，false 非空
 */
static bool rs_is_sector_empty(ring_storage_context_t* ctx, uint32_t sec_addr) {
    /* 采样检查：读扇区头部 16 字节即可判断（首帧 magic 必须在前 16B） */
    uint8_t buf[16];
    ring_storage_error_t err = (ring_storage_error_t)ring_storage_port_read(sec_addr, buf, sizeof(buf));

    if (err != RING_STORAGE_OK) {
        return false;
    }

    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0xFF) {
            return false;
        }
    }

    return true;
}

/**
 * @brief   扫描所有扇区，定位最新有效帧
 * @param   ctx 上下文指针
 * @return  操作结果错误码
 * @note    扫描策略：在每个扇区内顺序查找帧 magic（0x52535446），
 *          校验帧头 CRC16 和帧尾 commit_magic + data_crc32，
 *          选择 version 最大的有效帧。
 *          版本号比较使用差值法处理 32 位回绕。
 */
static ring_storage_error_t rs_scan_for_latest_frame(ring_storage_context_t* ctx) {
    const uint32_t sector_num = RS_SECTOR_NUM(ctx);
    const uint32_t sector_size = ctx->config.sector_size;
    /* 找到的最新帧信息 */
    uint32_t best_version = 0;
    uint32_t best_frame_addr = 0;
    uint32_t best_frame_flash_size = 0;
    uint32_t best_sector_addr = 0;
    bool found = false;

    /* 遍历所有扇区 */
    for (uint32_t s = 0; s < sector_num; s++) {
        uint32_t sec_addr = ctx->config.start_addr + s * sector_size;
        uint32_t sec_end = sec_addr + sector_size;

        /* 在扇区内扫描帧 */
        uint32_t scan_addr = sec_addr;

        while (scan_addr + RS_FRAME_OVERHEAD <= sec_end) {
            /* 读帧头 */
            rs_header_t hdr;
            ring_storage_error_t err = (ring_storage_error_t)
                ring_storage_port_read(scan_addr, (uint8_t*)&hdr, sizeof(hdr));

            if (err != RING_STORAGE_OK) {
                break; /* 读取失败，跳过该扇区 */
            }

            /* 检查 magic：0xFFFFFFFF 表示空白区，停止扫描该扇区 */
            if (hdr.magic == RS_FLASH_EMPTY) {
                break;
            }

            /* magic 不匹配，前进一个对齐单位继续查找 */
            if (hdr.magic != RS_FRAME_MAGIC) {
                scan_addr += RS_WRITE_ALIGN(ctx);
                continue;
            }

            /* magic 匹配，校验帧头 CRC16 */
            const uint16_t calc_crc = get_CRC16_check_sum((uint8_t*)&hdr,
                                                          offsetof(rs_header_t, header_crc16),
                                                          0xFFFF);
            if (calc_crc != hdr.header_crc16) {
                /* 帧头损坏，前进一个对齐单位 */
                scan_addr += RS_WRITE_ALIGN(ctx);
                continue;
            }

            /* 帧头有效，检查帧长度合法性 */
            if (hdr.frame_len < RS_FRAME_OVERHEAD || hdr.frame_len > sector_size) {
                scan_addr += RS_WRITE_ALIGN(ctx);
                continue;
            }

            /* 检查帧是否跨越扇区边界 */
            const uint32_t frame_end = scan_addr + hdr.frame_len;
            if (frame_end > sec_end) {
                scan_addr += RS_WRITE_ALIGN(ctx);
                continue;
            }

            /* 读帧尾，检查 commit_magic */
            rs_footer_t footer;
            const uint32_t footer_addr = scan_addr + hdr.frame_len - RS_FOOTER_SIZE;
            err = (ring_storage_error_t)
                ring_storage_port_read(footer_addr, (uint8_t*)&footer, sizeof(footer));

            if (err != RING_STORAGE_OK || footer.commit_magic != RS_COMMIT_MAGIC) {
                /* commit_magic 不匹配，帧未完成写入，前进 */
                scan_addr += RS_WRITE_ALIGN(ctx);
                continue;
            }

            /* 帧完整，选择 version 最大的 */
            if (!found || ((int32_t)(hdr.version - best_version) > 0)) {
                best_version = hdr.version;
                best_frame_addr = scan_addr;
                best_frame_flash_size = RS_FRAME_FLASH_SIZE(ctx, hdr.frame_len);
                best_sector_addr = sec_addr;
                found = true;
            }

            /* 前进到下一个帧位置（对齐后） */
            scan_addr += RS_FRAME_FLASH_SIZE(ctx, hdr.frame_len);
        }
    }

    if (!found) {
        /* 无有效帧，首次使用 */
        ctx->latest_version = 0;
        ctx->latest_frame_addr = 0;
        /* 选择第一个扇区作为活动扇区 */
        ctx->active_sector_addr = ctx->config.start_addr;
        ctx->write_offset = 0;

        /* 如果活动扇区不空（有垃圾数据），先擦除 */
        if (!rs_is_sector_empty(ctx, ctx->active_sector_addr)) {
            ring_storage_error_t erase_err = (ring_storage_error_t)
                ring_storage_port_erase(ctx->active_sector_addr, ctx->config.sector_size);
            if (erase_err != RING_STORAGE_OK) {
                DEBUG_LOGE("ring_storage", "首次使用：擦除活动扇区失败");
                return RING_STORAGE_ERROR_FLASH_ERASE;
            }
        }
        return RING_STORAGE_ERROR_NO_VALID_FRAME;
    }

    /* 设置运行时状态 */
    ctx->latest_version = best_version;
    ctx->latest_frame_addr = best_frame_addr;
    ctx->active_sector_addr = best_sector_addr;
    /* 写入偏移 = 最新帧末尾（对齐后） */
    ctx->write_offset = (best_frame_addr - best_sector_addr) + best_frame_flash_size;

    return RING_STORAGE_OK;
}

/**
 * @brief   垃圾回收：搬迁最新帧到空扇区，擦除旧扇区
 * @param   ctx 上下文指针
 * @return  操作结果错误码
 * @note    GC 策略：
 *          1. 找一个空白扇区
 *          2. 将最新帧复制到空白扇区起始
 *          3. 擦除原活动扇区
 *          4. 更新活动扇区和写入偏移
 */
static ring_storage_error_t rs_gc_collect(ring_storage_context_t* ctx) {
    const uint32_t sector_num = RS_SECTOR_NUM(ctx);
    const uint32_t sector_size = ctx->config.sector_size;
    const uint32_t write_align = RS_WRITE_ALIGN(ctx);

    /* 1. 查找空白扇区 */
    uint32_t empty_sec = 0;
    bool found_empty = false;

    for (uint32_t s = 0; s < sector_num; s++) {
        uint32_t sec_addr = ctx->config.start_addr + s * sector_size;
        if (sec_addr != ctx->active_sector_addr && rs_is_sector_empty(ctx, sec_addr)) {
            empty_sec = sec_addr;
            found_empty = true;
            break;
        }
    }

    if (!found_empty) {
        /* 无空白扇区，擦除一个非活动扇区作为空白扇区 */
        for (uint32_t s = 0; s < sector_num; s++) {
            uint32_t sec_addr = ctx->config.start_addr + s * sector_size;
            if (sec_addr != ctx->active_sector_addr) {
                empty_sec = sec_addr;
                found_empty = true;
                ring_storage_error_t err = (ring_storage_error_t)
                    ring_storage_port_erase(empty_sec, sector_size);
                if (err != RING_STORAGE_OK) {
                    DEBUG_LOGE("ring_storage", "GC 擦除扇区 0x%08X 失败", empty_sec);
                    return RING_STORAGE_ERROR_GC_FAILED;
                }
                break;
            }
        }
    }

    if (!found_empty) {
        DEBUG_LOGE("ring_storage", "GC 失败：无可用扇区");
        return RING_STORAGE_ERROR_GC_FAILED;
    }

    /* 2. 将最新帧复制到空白扇区 */
    if (ctx->latest_frame_addr != 0) {
        /* 读最新帧的帧头获取长度 */
        rs_header_t hdr;
        ring_storage_error_t err = (ring_storage_error_t)
            ring_storage_port_read(ctx->latest_frame_addr, (uint8_t*)&hdr, sizeof(hdr));

        if (err != RING_STORAGE_OK) {
            DEBUG_LOGE("ring_storage", "GC 读取帧头失败");
            return RING_STORAGE_ERROR_GC_FAILED;
        }

        const size_t flash_size = RS_FRAME_FLASH_SIZE(ctx, hdr.frame_len);
        const size_t buf_size = ctx->config.frame_buffer_size;

        /* 分块复制，每次 chunk 必须对齐到 write_align */
        uint32_t src = ctx->latest_frame_addr;
        uint32_t dst = empty_sec;
        size_t remaining = flash_size;

        while (remaining > 0) {
            size_t chunk = (remaining > buf_size) ? buf_size : remaining;
            /* 确保 chunk 对齐到写入颗粒度（最后一次除外） */
            if (chunk < remaining) {
                chunk = chunk - (chunk % write_align);
                if (chunk == 0) {
                    chunk = write_align; /* 缓冲区小于一个对齐单元，异常 */
                }
            }
            err = (ring_storage_error_t)ring_storage_port_read(src, ctx->config.frame_buffer, chunk);
            if (err != RING_STORAGE_OK) {
                DEBUG_LOGE("ring_storage", "GC 读数据失败 @0x%08X", src);
                return RING_STORAGE_ERROR_GC_FAILED;
            }
            err = (ring_storage_error_t)ring_storage_port_write(dst, ctx->config.frame_buffer, chunk);
            if (err != RING_STORAGE_OK) {
                DEBUG_LOGE("ring_storage", "GC 写数据失败 @0x%08X", dst);
                return RING_STORAGE_ERROR_GC_FAILED;
            }
            src += chunk;
            dst += chunk;
            remaining -= chunk;
        }

        /* 更新最新帧地址 */
        ctx->latest_frame_addr = empty_sec;
        ctx->write_offset = flash_size;
    } else {
        /* 无最新帧，空白扇区作为新起始 */
        ctx->write_offset = 0;
    }

    /* 3. 擦除原活动扇区 */
    uint32_t old_sec = ctx->active_sector_addr;
    if (old_sec != empty_sec) {
        ring_storage_error_t err = (ring_storage_error_t)
            ring_storage_port_erase(old_sec, sector_size);
        if (err != RING_STORAGE_OK) {
            DEBUG_LOGE("ring_storage", "GC 擦除旧扇区 0x%08X 失败", old_sec);
            return RING_STORAGE_ERROR_GC_FAILED;
        }
    }

    /* 4. 切换活动扇区 */
    ctx->active_sector_addr = empty_sec;

    DEBUG_LOGI("ring_storage", "GC 完成：活动扇区 0x%08X, 偏移 %u",
               ctx->active_sector_addr, ctx->write_offset);

    return RING_STORAGE_OK;
}

/**
 * @brief   序列化所有注册的 KV 到缓冲区
 * @param   ctx      上下文指针
 * @param   buf      输出缓冲区
 * @param   buf_size 缓冲区大小
 * @return  KV 数据区长度，0 表示失败
 */
static size_t rs_serialize_kv(ring_storage_context_t* ctx, uint8_t* buf, size_t buf_size) {
    size_t offset = 0;

    for (uint8_t i = 0; i < ctx->kv_count; i++) {
        const ring_storage_kv_entry_t* kv = &ctx->kv_table[i];
        const size_t key_len = strlen(kv->key);

        /* 检查缓冲区空间：key_len(1) + key + val_len(2) + value */
        const size_t needed = 1 + key_len + 2 + kv->value_len;
        if (offset + needed > buf_size) {
            DEBUG_LOGE("ring_storage", "序列化缓冲区溢出");
            return 0;
        }

        /* 写 key_len */
        buf[offset++] = (uint8_t)key_len;
        /* 写 key */
        memcpy(buf + offset, kv->key, key_len);
        offset += key_len;
        /* 写 val_len（小端） */
        buf[offset++] = (uint8_t)(kv->value_len & 0xFF);
        buf[offset++] = (uint8_t)((kv->value_len >> 8) & 0xFF);
        /* 写 value */
        memcpy(buf + offset, kv->value, kv->value_len);
        offset += kv->value_len;
    }

    return offset;
}

/**
 * @brief   解析帧数据并加载到注册的 KV
 * @param   ctx       上下文指针
 * @param   data      KV 数据区指针
 * @param   data_len  KV 数据区长度
 * @param   kv_count  帧中声明的 KV 数量
 * @return  操作结果错误码
 */
static ring_storage_error_t rs_parse_and_load_kv(ring_storage_context_t* ctx,
                                                  const uint8_t* data, size_t data_len,
                                                  uint16_t kv_count) {
    size_t offset = 0;

    for (uint16_t i = 0; i < kv_count; i++) {
        /* 读 key_len */
        if (offset + 1 > data_len) {
            return RING_STORAGE_ERROR_CRC;
        }
        const uint8_t key_len = data[offset++];

        /* 读 key */
        if (offset + key_len > data_len) {
            return RING_STORAGE_ERROR_CRC;
        }
        const char* key = (const char*)(data + offset);
        offset += key_len;

        /* 读 val_len */
        if (offset + 2 > data_len) {
            return RING_STORAGE_ERROR_CRC;
        }
        const uint16_t val_len = data[offset] | (data[offset + 1] << 8);
        offset += 2;

        /* 读 value */
        if (offset + val_len > data_len) {
            return RING_STORAGE_ERROR_CRC;
        }

        /* 在注册表中查找匹配的 key */
        for (uint8_t j = 0; j < ctx->kv_count; j++) {
            ring_storage_kv_entry_t* kv = &ctx->kv_table[j];
            if (strlen(kv->key) == key_len
                && strncmp(kv->key, key, key_len) == 0) {
                /* 长度匹配才复制，防止缓冲区溢出 */
                if (val_len == kv->value_len) {
                    memcpy(kv->value, data + offset, val_len);
                } else if (val_len < kv->value_len) {
                    /* 帧中数据较短，补零复制 */
                    memset(kv->value, 0, kv->value_len);
                    memcpy(kv->value, data + offset, val_len);
                }
                /* 帧中数据较长则截断 */
                break;
            }
        }

        offset += val_len;
    }

    return RING_STORAGE_OK;
}

/* Exported functions --------------------------------------------------------*/

ring_storage_error_t ring_storage_init(ring_storage_context_t* ctx,
                                       const ring_storage_config_t* config) {
    /* 参数检查 */
    if (ctx == NULL || config == NULL) {
        return RING_STORAGE_ERROR_NULL_PTR;
    }

    if (config->frame_buffer == NULL || config->frame_buffer_size == 0) {
        return RING_STORAGE_ERROR_NULL_PTR;
    }

    if (config->area_size < 2 * config->sector_size) {
        return RING_STORAGE_ERROR_INVALID_PARAM;
    }

    if (config->area_size % config->sector_size != 0) {
        return RING_STORAGE_ERROR_INVALID_PARAM;
    }

    if (config->start_addr % config->sector_size != 0) {
        return RING_STORAGE_ERROR_INVALID_PARAM;
    }

    /* 写入颗粒度检查 */
    if (config->write_gran != 8 && config->write_gran != 32
        && config->write_gran != 64 && config->write_gran != 128
        && config->write_gran != 256) {
        return RING_STORAGE_ERROR_INVALID_PARAM;
    }

    /* 如果已初始化，先反初始化 */
    if (ctx->initialized) {
        ring_storage_deinit(ctx);
    }

    /* 保存配置 */
    ctx->config = *config;

    /* 清空 KV 注册表 */
    ctx->kv_count = 0;
    memset(ctx->kv_table, 0, sizeof(ctx->kv_table));

    /* 扫描 Flash 定位最新帧 */
    ring_storage_error_t err = rs_scan_for_latest_frame(ctx);

    if (err == RING_STORAGE_ERROR_NO_VALID_FRAME) {
        DEBUG_LOGI("ring_storage", "首次使用：无有效帧，活动扇区 0x%08X",
                   ctx->active_sector_addr);
    } else if (err == RING_STORAGE_OK) {
        DEBUG_LOGI("ring_storage", "初始化成功：版本 %u, 帧@0x%08X, 扇区 0x%08X, 偏移 %u",
                   ctx->latest_version, ctx->latest_frame_addr,
                   ctx->active_sector_addr, ctx->write_offset);
    } else {
        DEBUG_LOGE("ring_storage", "初始化扫描失败: %d", err);
        return err;
    }

    ctx->initialized = true;

    return RING_STORAGE_OK;
}

void ring_storage_deinit(ring_storage_context_t* ctx) {
    if (ctx == NULL) {
        return;
    }

    ctx->kv_count = 0;
    ctx->initialized = false;
    ctx->latest_version = 0;
    ctx->latest_frame_addr = 0;
    ctx->write_offset = 0;
    ctx->active_sector_addr = 0;
}

bool ring_storage_is_initialized(const ring_storage_context_t* ctx) {
    return (ctx != NULL && ctx->initialized);
}

ring_storage_error_t ring_storage_register(ring_storage_context_t* ctx,
                                           const char* key,
                                           void* value,
                                           uint16_t value_len) {
    if (ctx == NULL || key == NULL || value == NULL) {
        return RING_STORAGE_ERROR_NULL_PTR;
    }

    if (!ctx->initialized) {
        return RING_STORAGE_ERROR_UNINITIALIZED;
    }

    /* 检查 key 长度 */
    const size_t key_len = strlen(key);
    if (key_len == 0 || key_len > RING_STORAGE_KEY_MAX) {
        return RING_STORAGE_ERROR_KEY_TOO_LONG;
    }

    /* 检查注册表是否已满 */
    if (ctx->kv_count >= RING_STORAGE_MAX_KV) {
        return RING_STORAGE_ERROR_KV_TABLE_FULL;
    }

    /* 检查 key 是否重复 */
    for (uint8_t i = 0; i < ctx->kv_count; i++) {
        if (strcmp(ctx->kv_table[i].key, key) == 0) {
            return RING_STORAGE_ERROR_KV_DUPLICATE;
        }
    }

    /* 注册 */
    ctx->kv_table[ctx->kv_count].key = key;
    ctx->kv_table[ctx->kv_count].value = value;
    ctx->kv_table[ctx->kv_count].value_len = value_len;
    ctx->kv_count++;

    return RING_STORAGE_OK;
}

ring_storage_error_t ring_storage_save(ring_storage_context_t* ctx) {
    if (ctx == NULL) {
        return RING_STORAGE_ERROR_NULL_PTR;
    }

    if (!ctx->initialized) {
        return RING_STORAGE_ERROR_UNINITIALIZED;
    }

    if (ctx->kv_count == 0) {
        return RING_STORAGE_ERROR_INVALID_PARAM;
    }

    ring_storage_port_lock();

    uint8_t* const buf = ctx->config.frame_buffer;
    const size_t buf_size = ctx->config.frame_buffer_size;

    /* 1. 预估帧大小，检查是否需要 GC */
    size_t est_kv_len = 0;
    for (uint8_t i = 0; i < ctx->kv_count; i++) {
        est_kv_len += 1 + strlen(ctx->kv_table[i].key) + 2 + ctx->kv_table[i].value_len;
    }
    const size_t est_frame_len = RS_FRAME_OVERHEAD + est_kv_len;
    const size_t est_flash_size = RS_FRAME_FLASH_SIZE(ctx, est_frame_len);
    const uint32_t sec_remain = ctx->config.sector_size - ctx->write_offset;

    if (est_flash_size > sec_remain) {
        DEBUG_LOGI("ring_storage", "扇区空间不足（剩 %u 需 %u），触发 GC",
                   sec_remain, est_flash_size);
        ring_storage_error_t gc_err = rs_gc_collect(ctx);
        if (gc_err != RING_STORAGE_OK) {
            ring_storage_port_unlock();
            return gc_err;
        }
    }

    /* 2. GC 完成后在帧缓冲区中组装完整帧（避免 GC 覆盖缓冲区） */
    const size_t kv_data_len = rs_serialize_kv(ctx, buf + RS_HEADER_SIZE,
                                                buf_size - RS_FRAME_OVERHEAD);
    if (kv_data_len == 0) {
        ring_storage_port_unlock();
        return RING_STORAGE_ERROR_BUFFER_TOO_SMALL;
    }

    /* 计算帧总长度 */
    const size_t frame_len = RS_FRAME_OVERHEAD + kv_data_len;

    /* 检查帧缓冲区是否足够 */
    if (frame_len > buf_size) {
        ring_storage_port_unlock();
        return RING_STORAGE_ERROR_BUFFER_TOO_SMALL;
    }

    /* 填充帧头 */
    rs_header_t* hdr = (rs_header_t*)buf;
    hdr->magic = RS_FRAME_MAGIC;
    hdr->version = ctx->latest_version + 1; /* 版本号递增 */
    hdr->frame_len = (uint32_t)frame_len;
    hdr->kv_count = ctx->kv_count;
    hdr->header_crc16 = get_CRC16_check_sum(buf,
                                            offsetof(rs_header_t, header_crc16),
                                            0xFFFF);

    /* 计算数据 CRC32 */
    const uint32_t data_crc = rs_calc_crc32(buf + RS_HEADER_SIZE, kv_data_len);

    /* 填充帧尾 */
    rs_footer_t* footer = (rs_footer_t*)(buf + RS_HEADER_SIZE + kv_data_len);
    footer->data_crc32 = data_crc;
    footer->commit_magic = RS_COMMIT_MAGIC;

    /* 3. 计算对齐后占用空间 */
    const size_t flash_size = RS_FRAME_FLASH_SIZE(ctx, frame_len);

    /* 4. 写入 Flash（分两步：帧体，最后帧尾作为原子提交点） */
    const uint32_t write_addr = RS_WRITE_ADDR(ctx);

    /* STM32G4 双字编程下，data_crc32(4B) + commit_magic(4B) = 8B 恰好一个双字，
     * 一次写入是原子的。先写帧体（帧头+KV数据），再写帧尾（提交点） */
    const size_t body_len = frame_len - RS_FOOTER_SIZE;

    ring_storage_error_t err = rs_align_write(ctx, write_addr, buf, body_len);
    if (err != RING_STORAGE_OK) {
        DEBUG_LOGE("ring_storage", "写入帧体失败 @0x%08X", write_addr);
        ring_storage_port_unlock();
        return err;
    }

    /* 写入帧尾（data_crc32 + commit_magic，原子提交点） */
    err = rs_align_write(ctx, write_addr + body_len,
                         (uint8_t*)footer, RS_FOOTER_SIZE);
    if (err != RING_STORAGE_OK) {
        DEBUG_LOGE("ring_storage", "写入帧尾失败 @0x%08X", write_addr + body_len);
        ring_storage_port_unlock();
        return err;
    }

    /* 5. 更新运行时状态 */
    ctx->latest_version = hdr->version;
    ctx->latest_frame_addr = write_addr;
    ctx->write_offset += flash_size;

    DEBUG_LOGI("ring_storage", "保存成功：版本 %u, 帧@0x%08X, 大小 %u/%u",
               ctx->latest_version, write_addr, frame_len, flash_size);

    ring_storage_port_unlock();
    return RING_STORAGE_OK;
}

ring_storage_error_t ring_storage_load(ring_storage_context_t* ctx) {
    if (ctx == NULL) {
        return RING_STORAGE_ERROR_NULL_PTR;
    }

    if (!ctx->initialized) {
        return RING_STORAGE_ERROR_UNINITIALIZED;
    }

    /* 无有效帧（首次使用） */
    if (ctx->latest_frame_addr == 0) {
        return RING_STORAGE_ERROR_NO_VALID_FRAME;
    }

    ring_storage_port_lock();

    /* 读帧头 */
    rs_header_t hdr;
    ring_storage_error_t err = (ring_storage_error_t)
        ring_storage_port_read(ctx->latest_frame_addr, (uint8_t*)&hdr, sizeof(hdr));

    if (err != RING_STORAGE_OK) {
        DEBUG_LOGE("ring_storage", "加载：读取帧头失败");
        ring_storage_port_unlock();
        return RING_STORAGE_ERROR_FLASH_READ;
    }

    /* 校验帧头 */
    if (hdr.magic != RS_FRAME_MAGIC) {
        ring_storage_port_unlock();
        return RING_STORAGE_ERROR_CRC;
    }

    const uint16_t calc_crc = get_CRC16_check_sum((uint8_t*)&hdr,
                                                  offsetof(rs_header_t, header_crc16),
                                                  0xFFFF);
    if (calc_crc != hdr.header_crc16) {
        DEBUG_LOGE("ring_storage", "加载：帧头 CRC16 校验失败");
        ring_storage_port_unlock();
        return RING_STORAGE_ERROR_CRC;
    }

    /* 读帧尾，校验 commit_magic */
    rs_footer_t footer;
    const uint32_t footer_addr = ctx->latest_frame_addr + hdr.frame_len - RS_FOOTER_SIZE;
    err = (ring_storage_error_t)
        ring_storage_port_read(footer_addr, (uint8_t*)&footer, sizeof(footer));

    if (err != RING_STORAGE_OK || footer.commit_magic != RS_COMMIT_MAGIC) {
        DEBUG_LOGE("ring_storage", "加载：commit_magic 校验失败");
        ring_storage_port_unlock();
        return RING_STORAGE_ERROR_CRC;
    }

    /* 读 KV 数据区 */
    const size_t kv_data_len = hdr.frame_len - RS_FRAME_OVERHEAD;
    uint8_t* const buf = ctx->config.frame_buffer;

    if (kv_data_len > ctx->config.frame_buffer_size) {
        DEBUG_LOGE("ring_storage", "加载：KV 数据 %u 超过缓冲区 %u",
                   kv_data_len, ctx->config.frame_buffer_size);
        ring_storage_port_unlock();
        return RING_STORAGE_ERROR_BUFFER_TOO_SMALL;
    }

    err = (ring_storage_error_t)
        ring_storage_port_read(ctx->latest_frame_addr + RS_HEADER_SIZE, buf, kv_data_len);

    if (err != RING_STORAGE_OK) {
        ring_storage_port_unlock();
        return RING_STORAGE_ERROR_FLASH_READ;
    }

    /* 校验数据 CRC32 */
    const uint32_t calc_data_crc = rs_calc_crc32(buf, kv_data_len);
    if (calc_data_crc != footer.data_crc32) {
        DEBUG_LOGE("ring_storage", "加载：数据 CRC32 校验失败");
        ring_storage_port_unlock();
        return RING_STORAGE_ERROR_CRC;
    }

    /* 解析并加载 KV */
    err = rs_parse_and_load_kv(ctx, buf, kv_data_len, hdr.kv_count);

    ring_storage_port_unlock();

    if (err == RING_STORAGE_OK) {
        DEBUG_LOGI("ring_storage", "加载成功：版本 %u, %u 个 KV",
                   hdr.version, hdr.kv_count);
    }

    return err;
}
