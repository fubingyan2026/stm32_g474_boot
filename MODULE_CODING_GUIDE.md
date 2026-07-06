# C语言模块编程规范提示词

## 概述

本文档用于指导 AI 生成符合规范的 C 语言模块代码。**请严格遵循本规范生成代码**。

---

## 核心参考标准

### 遵循的规范

- **WebKit 代码风格指南**：遵循 [WebKit Code Style Guidelines](https://webkit.org/code-style-guidelines/)，包括缩进、大括号位置、命名、注释等格式化规范
- **MISRA C:2012**：遵循 MISRA C 规范（安全性与可靠性约束）

---

## 文件结构规范

### 目录结构

```
module_name/
├── module_name.h              # 模块头文件
├── module_name.c              # 模块实现
└── MODULE_STYLE_GUIDE.md      # 模块规范文档（可选）
```

### 头文件模板 (.h)

```c
//
// Created by <author> on <date>.
//

#ifndef __MODULE_NAME_H
#define __MODULE_NAME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

// 类型定义...

/* Exported constants --------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

// 函数声明...

#ifdef __cplusplus
}
#endif

#endif /* __MODULE_NAME_H */
```

### 源文件模板 (.c)

```c
//
// Created by <author> on <date>.
//

/**
 * @file    module_name.c
 * @author  <author>
 * @version V1.0.0
 * @date    <date>
 * @brief   模块功能简述
 * @attention
 *
 * Copyright (c) 2025 Company Name.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "module_name.h"

#include <string.h>

/* Private constants ---------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

// 导出函数实现...

/* Private functions ---------------------------------------------------------*/

// 私有函数实现...
```

---

## 代码组织顺序

所有文件必须按以下顺序组织代码：

```c
// 1. 文件头注释
// 2. 头文件包含 (/* Includes ------------------------------------------------------------------*/)
// 3. 私有宏定义 (/* Private constants ---------------------------------------------------------*/)
// 4. 私有变量 (/* Private variables ---------------------------------------------------------*/)
// 5. 私有函数原型 (/* Private function prototypes -----------------------------------------------*/)
// 6. 导出函数 (/* Exported functions --------------------------------------------------------*/)
// 7. 私有函数 (/* Private functions ---------------------------------------------------------*/)
```

---

## 命名规范

### 模块前缀

所有类型、函数、宏、常量必须使用统一的前缀，格式为 `模块名_`（小写）。

**示例**：模块名为 `protocol_parser`，前缀为 `protocol_parser_`

### 类型定义

| 类型     | 命名规范                       | 示例                         |
| -------- | ------------------------------ | ---------------------------- |
| 枚举     | 前缀 + 蛇形命名 (结尾为 _t)    | `protocol_parser_error_t`    |
| 结构体   | 前缀 + 蛇形命名 (结尾为 _t)    | `protocol_parser_config_t`   |
| 函数指针 | 前缀 + 蛇形命名 (结尾为 _cb_t) | `protocol_parser_check_cb_t` |

### 枚举值

```c
typedef enum {
  MODULE_NAME_OK = 0,                   /**< 操作成功 */
  MODULE_NAME_ERROR_NULL_PTR,           /**< 空指针错误 */
  MODULE_NAME_ERROR_INVALID_PARAM,      /**< 无效参数 */
  MODULE_NAME_ERROR_UNINITIALIZED,      /**< 未初始化 */
  MODULE_NAME_ERROR_GENERIC,            /**< 通用错误 */
} module_name_error_t;
```

**规则：**
- 使用大写蛇形命名
- 带有模块前缀（如 `MODULE_NAME_`）
- 第一个值为 `MODULE_NAME_OK = 0`
- 每个值都有详细的**中文**注释

### 函数命名

| 函数类型     | 命名规范             | 示例                     |
| ------------ | -------------------- | ------------------------ |
| 公共函数     | 前缀 + 动词/动词短语 | `protocol_parser_init()` |
| 私有静态函数 | 模块名_ + 功能描述   | `parser_check_header()`  |

### 变量命名

| 变量类型      | 命名规范     | 示例                |
| ------------- | ------------ | ------------------- |
| 局部变量      | 小写蛇形命名 | `payload_len`       |
| 全局/静态变量 | 小写蛇形命名 | `s_uart1_rx_buffer` |
| 常量/宏       | 大写蛇形命名 | `IDLE_TIMER_US`     |

---

## 注释规范

### 中文注释要求

**所有公共 API 和复杂逻辑必须使用中文注释**。

### 枚举注释

```c
/**
 * @brief 协议解析器错误码枚举
 */
typedef enum {
  PROTOCOL_PARSER_OK = 0,                 /**< 操作成功 */
  PROTOCOL_PARSER_ERROR_NULL_PTR,         /**< 空指针错误 */
  PROTOCOL_PARSER_ERROR_BUFFER_OVERFLOW,  /**< 缓冲区溢出 */
} protocol_parser_error_t;
```

### 结构体注释

```c
/**
 * @brief 协议解析器配置结构体
 */
typedef struct {
  const char* name;                        /**< 协议名称 */
  const uint8_t* header;                   /**< 帧头数据 */
  uint16_t header_len;                     /**< 帧头长度 */
} protocol_parser_config_t;
```

### 函数注释

```c
/**
 * @brief 初始化协议解析器
 * @param ctx 协议解析器上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
protocol_parser_error_t protocol_parser_init(
    protocol_parser_context_t* ctx,
    const protocol_parser_config_t* config);
```

### 行内注释

```c
// 检查参数有效性
if (!ctx || !config) {
  return PROTOCOL_PARSER_ERROR_NULL_PTR;
}

// 保存配置
ctx->config = *config;
```

---

## 类型设计规范

### 配置结构体 (config_t)

配置结构体用于存储模块初始化时确定的所有配置参数，**包括回调函数**。

```c
typedef struct {
  // 基本配置参数
  const char* name;                        /**< 模块名称 */
  const uint8_t* header;                   /**< 帧头数据 */
  const uint8_t* footer;                   /**< 帧尾数据 */
  uint8_t* output_buffer;                  /**< 输出缓冲区 */

  // 回调函数（属于配置，初始化时确定）
  module_get_len_cb_t get_len_cb;          /**< 帧长度计算回调 */
  module_check_cb_t check_cb;              /**< 帧校验回调 */

  // 尺寸参数
  uint16_t header_len;                     /**< 帧头长度 */
  uint16_t footer_len;                     /**< 帧尾长度 */
  uint16_t input_buffer_len;               /**< 输入缓冲区大小 */
  uint16_t output_buffer_len;              /**< 输出缓冲区大小 */
} module_config_t;
```

**规则：**
- 配置参数集中存储
- 回调函数放在配置结构体中
- 使用 `const` 修饰只读指针
- 所有成员都有中文注释

### 上下文结构体 (context_t)

上下文结构体用于存储模块运行时状态，**包含配置结构体实例**。

```c
typedef struct module_context module_context_t;

struct module_context {
  module_config_t config;    /**< 配置参数（嵌套结构体） */

  // 运行时状态
  void* fifo;                /**< 内部FIFO缓冲区 */
  uint32_t idle_timer;       /**< 空闲计时器 */
  uint32_t tick_count;       /**< 系统计时器 */
  bool header_matched;       /**< 帧头匹配标志 */
  bool initialized;          /**< 初始化标志 */
};
```

**规则：**
- 使用前向声明 `typedef struct module_context module_context_t;`
- 配置参数嵌套存储在 `config` 成员中
- 运行时状态与配置分离
- 所有成员都有中文注释

### 访问方式

```c
// 访问配置参数
ctx->config.name
ctx->config.header_len
ctx->config.get_len_cb

// 访问运行时状态
ctx->initialized
ctx->idle_timer
```

---

## 函数设计规范

### 初始化函数

```c
/**
 * @brief 初始化模块
 * @param ctx 模块上下文指针
 * @param config 配置结构体指针
 * @return 操作结果错误码
 */
module_error_t module_init(module_context_t* ctx,
                           const module_config_t* config) {
  // 1. 检查参数有效性
  if (!ctx || !config) {
    return MODULE_ERROR_NULL_PTR;
  }

  if (!config->output_buffer) {
    return MODULE_ERROR_NULL_PTR;
  }

  // 2. 处理特殊情况
  uint16_t header_len = config->header_len;
  if (config->header == NULL) {
    header_len = 0;
  }

  // 3. 检查配置有效性
  if (config->output_buffer_len < (header_len + 1)) {
    return MODULE_ERROR_BUFFER_TOO_SMALL;
  }

  // 4. 如果已初始化，先反初始化
  if (ctx->initialized) {
    module_deinit(ctx);
  }

  // 5. 保存配置（整体复制）
  ctx->config = *config;
  ctx->config.header_len = header_len;  // 覆盖处理后的值

  // 6. 初始化运行时状态
  ctx->header_matched = false;
  ctx->idle_timer = 0;

  // 7. 分配资源
  ctx->fifo = kfifo_alloc(config->input_buffer_len, NULL);
  if (ctx->fifo == NULL) {
    return MODULE_ERROR_MEMORY_ALLOC;
  }

  // 8. 标记初始化完成
  ctx->initialized = true;

  return MODULE_OK;
}
```

### 反初始化函数

```c
/**
 * @brief 反初始化模块
 * @param ctx 模块上下文指针
 */
void module_deinit(module_context_t* ctx) {
  if (!ctx) {
    return;
  }

  // 释放资源
  if (ctx->fifo != NULL) {
    kfifo_free((kfifo_t*)ctx->fifo);
    ctx->fifo = NULL;
  }

  // 重置状态
  module_clear(ctx);
  ctx->initialized = false;
}
```

### 状态检查函数

```c
/**
 * @brief 检查模块是否已初始化
 * @param ctx 模块上下文指针
 * @return true表示已初始化，false表示未初始化
 */
bool module_is_initialized(const module_context_t* ctx) {
  return (ctx && ctx->initialized);
}
```

---

## 错误处理规范

### 参数检查

```c
// 检查空指针
if (!ctx || !config) {
  return MODULE_ERROR_NULL_PTR;
}

// 检查未初始化
if (!ctx->initialized) {
  return MODULE_ERROR_UNINITIALIZED;
}

// 检查无效参数
if (len == 0) {
  return MODULE_ERROR_INVALID_PARAM;
}
```

### 未使用参数

```c
static module_error_t internal_function(module_context_t* ctx,
                                        uint8_t param) {
  (void)ctx;  // 显式标记未使用的参数
  // ...
  return MODULE_OK;
}
```

### 错误返回

```c
// 使用枚举错误码
module_error_t result = module_do_something(ctx);
if (result != MODULE_OK) {
  return result;
}
```

---

## 内存管理规范

### 动态内存分配必须初始化

**所有动态分配的内存必须立即初始化为零**，未初始化的内存内容是未定义的，可能导致严重问题。

#### 问题示例

```c
// ❌ 错误：未初始化，可能导致死机
context_t* ctx = (context_t*)__malloc(sizeof(context_t));
// ctx->initialized 可能是垃圾值（如 0x5A）
// ctx->fifo 可能是垃圾指针

module_init(ctx, &config);  // 如果 ctx->initialized 是非零垃圾值，
                            // module_init 内部可能调用 module_deinit，
                            // 进而访问垃圾指针 ctx->fifo 导致死机
```

#### 正确做法

```c
// ✅ 正确：分配后立即清零
context_t* ctx = (context_t*)__malloc(sizeof(context_t));
if (ctx == NULL) {
  return MODULE_ERROR_MEMORY_ALLOC;
}
memset(ctx, 0, sizeof(context_t));  // 立即清零

// 现在所有成员都是确定的初始值：
// ctx->initialized = false (0)
// ctx->fifo = NULL (0)
// ctx->idle_timer = 0
```

#### 根因分析

当 `__malloc` 返回的内存未初始化时：

1. `ctx->initialized` 可能是非零垃圾值
2. `module_init()` 检查到 `ctx->initialized == true`
3. `module_init()` 调用 `module_deinit(ctx)` 尝试清理
4. `module_deinit()` 访问垃圾指针 `ctx->fifo`
5. **访问非法内存 → 程序死机**

#### 规则总结

| 规则             | 说明                                                                |
| ---------------- | ------------------------------------------------------------------- |
| 分配后立即初始化 | `__malloc` 后必须紧跟 `memset(ptr, 0, size)`                      |
| 检查返回值       | 分配后必须检查是否为 `NULL`                                         |
| 结构体优先清零   | 所有结构体分配后都应清零，确保指针成员为 `NULL`，布尔成员为 `false` |

#### 代码模板

```c
/**
 * @brief 创建模块实例
 * @param config 配置结构体指针
 * @return 成功返回实例指针，失败返回NULL
 */
module_context_t* module_create(const module_config_t* config) {
  // 1. 分配内存
  module_context_t* ctx = (module_context_t*)__malloc(sizeof(module_context_t));
  if (ctx == NULL) {
    return NULL;
  }

  // 2. 立即初始化为零（关键！）
  memset(ctx, 0, sizeof(module_context_t));

  // 3. 保存配置
  ctx->config = *config;

  // 4. 分配其他资源
  ctx->fifo = kfifo_alloc(config->buffer_size, NULL);
  if (ctx->fifo == NULL) {
    __free(ctx);
    return NULL;
  }

  // 5. 标记初始化完成
  ctx->initialized = true;

  return ctx;
}
```

---

## 代码格式规范

本模块遵循 **WebKit 代码风格**。

### 缩进

- 使用 **4 空格缩进**
- 不要使用 Tab

### 大括号位置

- **函数定义**：左大括号**另起一行**（Allman 风格）
- **控制语句**（if/for/while）：左大括号**与关键字同行**

```c
// 函数定义（Allman 风格）
module_error_t module_init(module_context_t* ctx,
    const module_config_t* config)
{
    // ...
}

// 控制语句（K&R 风格）
if (condition) {
    // ...
} else {
    // ...
}

// 循环
for (uint32_t i = 0; i < count; i++) {
    // ...
}

while (condition) {
    // ...
}
```

### 行长

- 单行不超过 **100 字符**
- 超长行需要换行并对齐

### 函数声明换行

```c
// 参数较多时换行
module_error_t module_feed(module_context_t* ctx,
                           const uint8_t* data,
                           uint32_t len);

// 返回类型较长时换行
const uint8_t* module_parse(module_context_t* ctx,
                            uint16_t* len);
```

---

## 类型安全规范

### 使用固定宽度整数

```c
#include <stdint.h>

uint8_t   // 8位无符号整数
uint16_t  // 16位无符号整数
uint32_t  // 32位无符号整数
int32_t   // 32位有符号整数
```

### 使用布尔类型

```c
#include <stdbool.h>

bool flag = true;
bool check_result = false;
```

### const 正确性

```c
// 输入参数，不修改内容
module_error_t module_init(module_context_t* ctx,
                           const module_config_t* config);

// 输出参数，用于返回结果
module_error_t module_read(module_context_t* ctx,
                           uint8_t* data,
                           uint32_t* len);

// 返回只读指针
const uint8_t* module_get_buffer(const module_context_t* ctx);
```

---

## 检查清单

生成代码时，请确认：

- [ ] 文件头注释完整
- [ ] 代码组织顺序正确
- [ ] 命名符合规范（前缀、蛇形命名）
- [ ] 所有公共 API 有中文注释
- [ ] 配置结构体包含所有配置参数（包括回调函数）
- [ ] 上下文结构体嵌套配置结构体
- [ ] 参数验证完整
- [ ] 错误码返回正确
- [ ] 未使用参数用 `(void)` 标记
- [ ] 使用固定宽度整数类型
- [ ] 代码格式符合要求（4空格缩进，WebKit风格）
- [ ] 无编译错误和警告
- [ ] **动态分配的内存已初始化为零**

---
