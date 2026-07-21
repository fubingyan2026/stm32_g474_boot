# ring_storage/

环形缓冲区 Flash 参数存储模块。专为嵌入式 MCU 参数持久化设计，替代 EasyFlash NG 用于参数存储场景。

## 结构

```
ring_storage/
├── ring_storage.h           # 公共 API（类型、错误码、函数声明）
├── ring_storage_port.h      # 平台抽象接口（用户实现 read/write/erase/lock）
├── ring_storage.c           # 核心实现（帧打包/解析/扫描/GC）
└── AGENTS.md                # 本文档
```

## 核心设计

### 帧格式

```
偏移    字段             大小    说明
----------------------------------------------
 0      magic            4B     0x52535446 ("RSTF")
 4      version          4B     单调递增版本号
 8      frame_len        4B     帧总逻辑大小（含头尾）
12      kv_count         2B     KV 条目数
14      header_crc16     2B     帧头 CRC16（偏移 0~13）
16      KV 数据区        N B     [klen(1)][key][vlen(2)][val]...
16+N   data_crc32       4B     KV 数据区 CRC32
20+N   commit_magic     4B     0x434F4D54 ("COMT") 原子提交点
----------------------------------------------
固定开销：24B/帧
```

### 断电保护

- **帧头 CRC16**：防止帧头损坏导致越界读取
- **数据 CRC32**：保证 KV 数据完整性
- **commit_magic**：最后写入的原子提交点，未写入则帧视为不完整

### 磨损均衡

- 多扇区轮转写入，写满后 GC 搬迁最新帧到空扇区
- 擦除旧扇区，实现磨损均衡

### 颗粒度自适应

支持 8/32/64/128/256bit 写入颗粒度，通过 `align_write` 函数处理对齐填充。

## 使用方法

```c
#include "ring_storage.h"
#include "ring_storage_port.h"

/* 1. 实现平台接口（ring_storage_port.h） */
/* 2. 创建实例 */

static uint8_t s_frame_buf[1024];
static ring_storage_context_t s_storage;

const ring_storage_config_t cfg = {
    .start_addr        = 0x08078000,
    .area_size         = 8192,
    .sector_size       = 2048,
    .write_gran        = 64,
    .frame_buffer      = s_frame_buf,
    .frame_buffer_size = sizeof(s_frame_buf),
};

ring_storage_init(&s_storage, &cfg);
ring_storage_register(&s_storage, "motor_poles", &g_poles, sizeof(g_poles));
ring_storage_register(&s_storage, "pid_kp", &g_kp, sizeof(g_kp));

ring_storage_load(&s_storage);  /* 启动时加载 */
ring_storage_save(&s_storage);  /* 修改后保存 */
```

## 与 EasyFlash 对比

| 特性          | EasyFlash NG      | ring_storage      |
|---------------|-------------------|-------------------|
| 模型          | 单 KV 独立存储    | 整包快照存储      |
| 固定开销      | 48B/ENV（64bit）  | 24B/帧            |
| 查找          | 全扫描/缓存       | O(1) 内存注册表   |
| GC            | 逐个搬迁 ENV      | 整帧复制          |
| 断电保护      | PRE_WRITE+WRITE   | commit_magic 单点 |
| 适用场景      | KV 多、增删频繁   | 参数集小、批量保存|

## 依赖

- `algorithm/crc.h`：CRC16 校验（帧头）
- `utils/memory_pool.h`：`__memset`/`__memcpy`/`__strlen` 等
- `services/debug/debug.h`：日志输出
