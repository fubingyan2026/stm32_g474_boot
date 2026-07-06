//
// Created by fubingyan on 25-4-5.
//

/**
 * @file    daemon.c
 * @brief   守护进程模块实现
 * @note    通过 clist 双向循环链表管理守护进程实例。
 *          每个实例维护喂狗时间戳，daemon_task() 定期检查超时状态。
 */

/* Includes ------------------------------------------------------------------*/
#include "daemon.h"

#include <string.h>

#include "public.h"

/* Private variables ---------------------------------------------------------*/

static clist_head_t s_daemon_head; /**< 守护进程链表头 */
static bool s_daemon_initialized = false; /**< 系统初始化标志 */
static daemon_get_time_cb_t s_get_time_cb = NULL; /**< 时间戳获取回调 */

/* Private function prototypes -----------------------------------------------*/

static inline uint32_t daemon_get_time(void);
static inline uint32_t daemon_time_diff(uint32_t new_time, uint32_t old_time);

/* Exported functions --------------------------------------------------------*/

daemon_error_t daemon_init(daemon_get_time_cb_t get_time_cb)
{
    if (get_time_cb == NULL) {
        return DAEMON_ERROR_NULL_PTR;
    }
    if (s_daemon_initialized) {
        return DAEMON_OK_EXISTED;
    }
    s_get_time_cb = get_time_cb;
    clist_init(&s_daemon_head);
    s_daemon_initialized = true;
    return DAEMON_OK;
}

void daemon_deinit(void)
{
    if (!s_daemon_initialized)
        return;

    /* 安全遍历并释放所有节点 */
    clist_head_t *pos, *tmp;
    clist_for_each_safe(pos, tmp, &s_daemon_head)
    {
        daemon_context_t* ctx = clist_entry(pos, daemon_context_t, node);
        clist_del(pos);
        if (!ctx->is_static) {
            // __free(ctx);
        }
    }

    clist_init(&s_daemon_head);
    s_daemon_initialized = false;
}

/**
 * @brief 按名称查找守护进程
 */
daemon_context_t* daemon_get_instance(const char* name)
{
    if (name == NULL || !s_daemon_initialized)
        return NULL;

    daemon_context_t* ctx;
    clist_for_each_entry(ctx, &s_daemon_head, node)
    {
        if (strcmp(ctx->config.name, name) == 0)
            return ctx;
    }
    return NULL;
}

daemon_error_t daemon_register_static(const daemon_config_t* config,
    daemon_context_t* instance)
{
    if (config == NULL || config->name == NULL || instance == NULL) {
        return DAEMON_ERROR_NULL_PTR;
    }
    if (!s_daemon_initialized)
        return DAEMON_ERROR_UNINITIALIZED;
    if (daemon_get_instance(config->name) != NULL) {
        return DAEMON_ERROR_ALREADY_EXIST;
    }

    memset(instance, 0, sizeof(daemon_context_t));
    memcpy(&instance->config, config, sizeof(daemon_config_t));

    instance->current_feed_time = daemon_get_time();
    instance->last_feed_time = instance->current_feed_time;
    instance->online_last = true;
    instance->online = true;
    instance->is_static = true;

    clist_add_tail(&s_daemon_head, &instance->node);
    return DAEMON_OK;
}

daemon_error_t daemon_unregister(const char* name)
{
    if (name == NULL)
        return DAEMON_ERROR_NULL_PTR;
    if (!s_daemon_initialized)
        return DAEMON_ERROR_UNINITIALIZED;

    daemon_context_t* ctx;
    clist_for_each_entry(ctx, &s_daemon_head, node)
    {
        if (strcmp(ctx->config.name, name) == 0) {
            clist_del(&ctx->node);
            if (!ctx->is_static)
            {
                // __free(ctx);
            }
            return DAEMON_OK;
        }
    }
    return DAEMON_ERROR_NOT_FOUND;
}

uint16_t daemon_get_count(void)
{
    return (uint16_t)clist_size(&s_daemon_head);
}

clist_head_t* daemon_get_head(void)
{
    return s_daemon_initialized ? &s_daemon_head : NULL;
}

void daemon_reload(daemon_context_t* ctx)
{
    if (ctx == NULL)
        return;
    ctx->last_feed_time = ctx->current_feed_time;
    ctx->current_feed_time = daemon_get_time();
}

bool daemon_is_online(const daemon_context_t* ctx)
{
    return ctx ? ctx->online : false;
}

const char* daemon_get_name(const daemon_context_t* ctx)
{
    return ctx ? ctx->config.name : "not_found!";
}

float daemon_get_feed_frequency(const daemon_context_t* ctx)
{
    return ctx ? ctx->feed_frequency : 0.0f;
}

/**
 * @brief 守护进程任务：检测超时与状态变化
 * @note  每个守护进程经历三个阶段：
 *        1. 初始化等待（init_wait）：等待计数到达配置值后标记在线
 *        2. 超时检测：检查 current_feed_time 是否超过 reload_timeout_ms
 *        3. 稳定区间：通过多次采样防抖，确保状态稳定后才切换
 *
 *        当在线状态发生跳变时调用 offline_cb 回调。
 *        last_time 静态变量记录上次调用时间，用于计算 diff_time。
 *        first_run 标志防止首次调用时 diff_time 异常过大。
 */
void daemon_task(void)
{
    if (!s_daemon_initialized || clist_empty(&s_daemon_head))
        return;

    static uint32_t last_time = 0;
    static bool first_run = true;
    uint32_t current_time = daemon_get_time();
    uint32_t diff_time;

    if (first_run) {
        /* 首次调用：避免 last_time=0 导致 diff_time 异常 */
        first_run = false;
        diff_time = 1;
    } else {
        diff_time = daemon_time_diff(current_time, last_time);
        if (diff_time == 0)
            diff_time = 1;
    }
    last_time = current_time;

    daemon_context_t* ctx;
    clist_for_each_entry(ctx, &s_daemon_head, node)
    {
        /* 阶段1：初始化等待 */
        if (ctx->init_wait_counter <= ctx->config.init_wait_time_ms) {
            ctx->init_wait_counter += diff_time;
            if (ctx->init_wait_counter > ctx->config.init_wait_time_ms) {
                ctx->online = true;
            }
        }
        /* 阶段2：超时检测 */
        else if (daemon_time_diff(current_time, ctx->current_feed_time) > ctx->config.reload_timeout_ms) {
            if (ctx->config.reload_timeout_ms == 0xFFFF || ctx->config.reload_timeout_ms == 0) {
                ctx->online = true; /* 永不超时 */
            } else {
                ctx->online = false; /* 标记离线 */
            }
            ctx->rx_counter = 0;
        }
        /* 阶段3：稳定区间防抖 */
        else {
            uint32_t threshold = DAEMON_STABLE_TIMES_MS / diff_time;
            if (threshold == 0)
                threshold = 1;
            if (++ctx->rx_counter >= threshold) {
                ctx->rx_counter = 0;
                ctx->online = true;
            }
        }

        /* 计算喂狗频率 */
        if (ctx->current_feed_time > ctx->last_feed_time) {
            uint32_t td = daemon_time_diff(ctx->current_feed_time,
                ctx->last_feed_time);
            if (td > 0) {
                ctx->feed_frequency = 1000.0f / (float)td;
            }
        }

        /* 检测在线状态边沿变化，触发回调 */
        if (ctx->online_last != ctx->online) {
            if (ctx->config.offline_cb != NULL) {
                ctx->config.offline_cb(ctx->config.owner_ptr);
            }
            ctx->online_last = ctx->online;
        }
    }
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief 获取系统时间戳
 * @return 毫秒时间戳，回调为空时返回 0
 */
static inline uint32_t daemon_get_time(void)
{
    return s_get_time_cb ? s_get_time_cb() : 0;
}

/**
 * @brief 安全计算32位时间差（处理溢出回绕）
 * @param new_time 新时间戳
 * @param old_time 旧时间戳
 * @return 时间差(ms)
 */
static inline uint32_t daemon_time_diff(uint32_t new_time, uint32_t old_time)
{
    return (uint32_t)(new_time - old_time);
}
