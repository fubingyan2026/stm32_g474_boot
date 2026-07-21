/* mock debug.h for PC testing */
#ifndef MOCK_DEBUG_H
#define MOCK_DEBUG_H

#include <stdio.h>

#define DEBUG_LOGI(tag, fmt, ...)  printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define DEBUG_LOGE(tag, fmt, ...)  printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define DEBUG_LOGD(tag, fmt, ...)  printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)

typedef enum {
    DEBUG_LEVEL_INFO = 0,
    DEBUG_LEVEL_DEBUG,
    DEBUG_LEVEL_WARN,
    DEBUG_LEVEL_ERROR,
} debug_level_t;

#endif
