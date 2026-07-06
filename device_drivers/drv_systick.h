#ifndef HAL_SYSTICK_H
#define HAL_SYSTICK_H

#include "stdint.h"

void delay_init(void);

void delay_us(uint16_t nus);

void delay_ms(uint16_t nms);

uint32_t micros(void);

uint32_t millis(void);

#endif
