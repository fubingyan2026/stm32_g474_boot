//
// Created by maximillian on 2026-07-06.
//

#ifndef __DRV_STM32G4_FLASH_H
#define __DRV_STM32G4_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stddef.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

typedef enum {
    EF_NO_ERR = 0, /**< 操作成功 */
    EF_ERASE_ERR, /**< 擦除失败 */
    EF_READ_ERR, /**< 读取失败 */
    EF_WRITE_ERR, /**< 写入失败 */
    EF_ENV_INIT_FAILED, /**< 环境变量初始化失败 */
    EF_ENV_FULL, /**< 环境变量存储区已满 */
    EF_ENV_ERR, /**< 环境变量错误 */
} ef_err_code_t;

/* Exported macro ------------------------------------------------------------*/

#define EF_ASSERT(expr)      \
    do {                     \
        if (!(expr)) {       \
            __disable_irq(); \
            while (1) { }    \
        }                    \
    } while (0)

/* Exported functions prototypes ---------------------------------------------*/

void ef_port_init(void);
void ef_port_cache_invalidate(void);
ef_err_code_t ef_port_read(uint32_t addr, uint32_t* buf, size_t size);
ef_err_code_t ef_port_erase(uint32_t addr, size_t size);
ef_err_code_t ef_port_write(uint32_t addr, const uint32_t* buf, size_t size);
uint32_t ef_port_get_page_size(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_STM32G4_FLASH_H */
