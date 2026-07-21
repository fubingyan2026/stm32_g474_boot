/**
 * @file    rs_crc32.h
 * @brief   CRC32 计算模块（多项式 0xEDB88320，与 zlib/PNG 兼容）
 * @note    从 ring_storage 中独立拆分，供帧数据完整性校验使用
 */
#ifndef __RS_CRC32_H
#define __RS_CRC32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/**
 * @brief   计算 CRC32
 * @param   data 数据指针
 * @param   len  数据长度
 * @return  CRC32 值
 */
uint32_t rs_crc32(const void* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* __RS_CRC32_H */
