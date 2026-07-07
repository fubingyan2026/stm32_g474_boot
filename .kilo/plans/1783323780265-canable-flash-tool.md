# Flash 写入修复：PG 替代 FSTPG

## 问题

`ef_port_write` 使用 `FSTPG` 持续失败 (SR=0x2A0 = FASTERR+PGSERR+PGAERR)。`FSTPG` 是批量快速编程模式，STM32G4 规范要求配合特定的写入序列。单双字编程应用 `PG` 位。

## 修复

`device_drivers/drv_stm32g4_flash.c` 中 `ef_port_write`，将 `FLASH_CR_FSTPG` 改为 `FLASH_CR_PG`，并增加 EOP 清除：

```c
if (s_write_buf != FLASH_ERASED_VAL) {
    /* 单双字编程：PG 位，同时清除 EOP + SR + ECCR 标志 */
    FLASH->SR = FLASH_FLAG_EOP | FLASH_FLAG_SR_ERRORS;
    FLASH->ECCR = FLASH_FLAG_ECCC | FLASH_FLAG_ECCD;

    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PER | FLASH_CR_MER1 | FLASH_CR_MER2
                   | FLASH_CR_FSTPG | FLASH_CR_STRT | FLASH_CR_PNB
                   | FLASH_CR_BKER)) | FLASH_CR_PG;
    __DSB();

    *(volatile uint32_t*)addr     = (uint32_t)(s_write_buf & 0xFFFFFFFFULL);
    __DSB();
    *(volatile uint32_t*)(addr + 4U) = (uint32_t)(s_write_buf >> 32U);

    while (FLASH->SR & FLASH_FLAG_BSY) {}
    FLASH->CR &= ~FLASH_CR_PG;

    if (FLASH->SR & FLASH_FLAG_SR_ERRORS) {
        LOG_E("flash", "Flash 编程错误: i=%u, addr=0x%08lX, SR=0x%08lX",
            (unsigned)i, addr, (unsigned long)FLASH->SR);
        result = EF_WRITE_ERR;
        goto exit_write;
    }
}
```

## 构建验证

```bash
cmake --preset Debug && cmake --build build/Debug
```
