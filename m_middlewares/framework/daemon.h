//
// Created by fubingyan on 25-4-5.
//

/**
 * @file    daemon.h
 * @brief   守护进程模块 — 任务健康监控
 * @note    通过周期性喂狗机制监控任务在线状态，支持超时检测与离线回调。
 *          使用侵入式双向循环链表(clist)管理守护进程实例。
 */

#ifndef __DAEMON_H
#define __DAEMON_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "clist.h"

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 守护进程错误码
 */
typedef enum {
    DAEMON_OK = 0, /**< 操作成功 */
    DAEMON_OK_EXISTED, /**< 系统已初始化，无需重复操作 */
    DAEMON_ERROR_NULL_PTR, /**< 空指针错误 */
    DAEMON_ERROR_INVALID_PARAM, /**< 无效参数 */
    DAEMON_ERROR_NO_MEMORY, /**< 内存不足 */
    DAEMON_ERROR_NOT_FOUND, /**< 未找到指定守护进程 */
    DAEMON_ERROR_ALREADY_EXIST, /**< 同名守护进程已存在 */
    DAEMON_ERROR_UNINITIALIZED, /**< 系统未初始化 */
    DAEMON_ERROR_GENERIC, /**< 通用错误 */
} daemon_error_t;

/**
 * @brief 离线回调函数类型
 * @param owner_ptr 拥有者指针（config 中传入）
 */
typedef void (*daemon_offline_cb_t)(void* owner_ptr);

/**
 * @brief 获取系统时间戳回调
 * @return 系统时间戳，单位毫秒
 */
typedef uint32_t (*daemon_get_time_cb_t)(void);

/**
 * @brief 守护进程配置
 */
typedef struct {
    const char* name; /**< 守护进程名称（唯一标识） */
    void* owner_ptr; /**< 拥有者指针，离线回调时传回 */
    daemon_offline_cb_t offline_cb; /**< 离线回调函数（状态变化时触发） */
    uint16_t reload_timeout_ms; /**< 喂狗超时时间(ms)，0xFFFF 或 0 表示永不超时 */
    uint16_t init_wait_time_ms; /**< 初始化等待时间(ms)，期间不检测超时 */
} daemon_config_t;

/**
 * @brief 守护进程上下文（前向声明）
 */
typedef struct daemon_context daemon_context_t;

/**
 * @brief 守护进程上下文
 */
struct daemon_context {
    daemon_config_t config; /**< 配置参数副本 */
    clist_head_t node; /**< clist 链表节点 */

    uint32_t last_feed_time; /**< 上次喂狗时间戳 */
    uint32_t current_feed_time; /**< 当前喂狗时间戳 */
    float feed_frequency; /**< 喂狗频率(Hz) */
    uint32_t init_wait_counter; /**< 初始化等待计数器 */
    bool online; /**< 当前在线状态 */
    bool online_last; /**< 上次在线状态（用于检测边沿变化） */
    uint8_t rx_counter; /**< 稳定计数（防抖） */
    bool is_static; /**< 静态分配标志 */
};

/* Exported constants --------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

#define DAEMON_STABLE_TIMES_MS 10 /**< 稳定计数阈值(ms)：超过此时间视为稳定在线 */

#define DAEMON_IS_OK(err) ((err) >= 0) /**< 判断错误码是否表示成功 */
#define DAEMON_IS_ERR(err) ((err) < 0) /**< 判断错误码是否表示失败 */

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化守护进程系统
 * @param get_time_cb 获取系统毫秒时间戳的回调函数
 * @return DAEMON_OK 成功，DAEMON_ERROR_NULL_PTR 回调为空，DAEMON_OK_EXISTED 已初始化
 */
daemon_error_t daemon_init(daemon_get_time_cb_t get_time_cb);

/**
 * @brief 反初始化守护进程系统，释放所有动态分配的实例
 */
void daemon_deinit(void);

/**
 * @brief 静态注册守护进程（使用用户分配的内存）
 * @param config 配置指针
 * @param instance 用户提供的实例内存
 * @return 错误码
 */
daemon_error_t daemon_register_static(const daemon_config_t* config,
    daemon_context_t* instance);

/**
 * @brief 注销守护进程
 * @param name 守护进程名称
 * @return 错误码
 */
daemon_error_t daemon_unregister(const char* name);

/**
 * @brief 喂狗：刷新守护进程的时间戳
 * @param ctx 守护进程上下文指针
 */
void daemon_reload(daemon_context_t* ctx);

/**
 * @brief 检查守护进程是否在线
 * @param ctx 守护进程上下文指针
 * @return true 在线，false 离线或 ctx 为空
 */
bool daemon_is_online(const daemon_context_t* ctx);

/**
 * @brief 获取守护进程名称
 * @param ctx 守护进程上下文指针
 * @return 名称字符串，ctx 为空时返回 "not_found!"
 */
const char* daemon_get_name(const daemon_context_t* ctx);

/**
 * @brief 获取喂狗频率
 * @param ctx 守护进程上下文指针
 * @return 喂狗频率(Hz)，ctx 为空时返回 0.0f
 */
float daemon_get_feed_frequency(const daemon_context_t* ctx);

/**
 * @brief 守护进程任务函数
 * @note 需在主循环或定时器中定期调用。遍历所有守护进程检测超时，处理状态变化回调。
 */
void daemon_task(void);

/**
 * @brief 按名称查找守护进程实例
 * @param name 守护进程名称
 * @return 实例指针，未找到返回 NULL
 */
daemon_context_t* daemon_get_instance(const char* name);

/**
 * @brief 获取守护进程链表头
 * @return clist 链表头指针，未初始化返回 NULL
 */
clist_head_t* daemon_get_head(void);

/**
 * @brief 获取守护进程注册总数
 * @return 守护进程数量
 */
uint16_t daemon_get_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __DAEMON_H */
