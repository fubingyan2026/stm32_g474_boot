
#ifndef __FSM_H
#define __FSM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 状态标识符类型
 */
typedef uint8_t fsm_state_t;

/**
 * @brief 返回码枚举
 */
typedef enum {
    FSM_OK = 0, /**< 操作成功 */
    FSM_ERR, /**< 通用错误 */
    FSM_ERR_NULL, /**< 空指针错误 */
    FSM_ERR_STATE, /**< 无效状态ID */
    FSM_ERR_TRANS, /**< 无效状态转换 */
    FSM_ERR_HANDLER, /**< 状态没有注册处理器 */
    FSM_ERR_INIT, /**< 状态机尚未初始化 */
} fsm_err_t;

typedef struct fsm fsm_t;

/**
 * @brief 状态处理器函数类型
 * @param ctx 状态机上下文指针
 * @return 下一个状态（返回当前状态以保持）
 */
typedef fsm_state_t (*fsm_handler_t)(fsm_t* ctx);

/**
 * @brief 状态进入回调函数类型
 * @param ctx 状态机上下文指针
 * @param state 正在进入的状态
 */
typedef void (*fsm_entry_cb_t)(fsm_t* ctx, fsm_state_t state);

/**
 * @brief 状态退出回调函数类型
 * @param ctx 状态机上下文指针
 * @param state 正在退出的状态
 */
typedef void (*fsm_exit_cb_t)(fsm_t* ctx, fsm_state_t state);

/**
 * @brief 转换条件函数类型
 * @param ctx 状态机上下文指针
 * @return true表示允许转换，false表示禁止转换
 */
typedef bool (*fsm_guard_t)(const fsm_t* ctx);

/**
 * @brief 无条件转换哨兵函数
 *
 * 使用指向此函数的指针来区分"无转换规则"和"无条件转换规则"。
 * 在转换矩阵中，NULL 表示不存在转换，指向本函数表示无条件转换。
 */
static inline bool fsm_always_true(const fsm_t* ctx)
{
    (void)ctx;
    return true;
}

/**
 * @brief 状态机配置结构体
 *
 * 所有指针成员由用户在初始化前分配（栈、静态区或堆），fsm_init 不负责内存管理。
 * 不同实例可以指定不同的 state_count，无需全局编译期上限。
 */
typedef struct {
    fsm_handler_t* handlers; /**< 状态处理器函数表（用户分配，长度 state_count） */
    fsm_guard_t* transitions; /**< 转换条件扁平矩阵（用户分配，长度 state_count * state_count） */
    fsm_state_t state_count; /**< 实际状态数量 */

    fsm_entry_cb_t entry_cb; /**< 全局进入回调 */
    fsm_exit_cb_t exit_cb; /**< 全局退出回调 */

    const char** state_names; /**< 状态名称查找表 */

    void* user_data; /**< 用户自定义上下文数据 */
} fsm_config_t;

/**
 * @brief 状态机上下文结构体
 *
 * 存储状态机运行时状态，嵌套配置结构体。
 */
struct fsm {
    fsm_config_t config; /**< 配置参数（嵌套结构体） */

    fsm_state_t current_state; /**< 当前状态 */
    fsm_state_t last_state; /**< 上一个状态（与 current_state 比较可知是否变更） */
    bool initialized; /**< 初始化标志 */
};

/* Exported inline utilities -------------------------------------------------*/

/**
 * @brief 获取扁平矩阵中 (from, to) 位置的转换条件指针
 * @param config 配置结构体指针
 * @param from 源状态
 * @param to 目标状态
 * @return 转换条件指针，越界返回NULL
 */
static inline fsm_guard_t* fsm_at(const fsm_config_t* config,
    fsm_state_t from,
    fsm_state_t to)
{
    if (!config || !config->transitions) {
        return NULL;
    }
    if (from >= config->state_count || to >= config->state_count) {
        return NULL;
    }
    return &config->transitions[(uint16_t)from * config->state_count + to];
}

/* Exported config helpers --------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 初始化状态机上下文
 * @param ctx 状态机上下文指针
 * @param initial_state 初始状态
 * @param config 配置结构体指针（handlers 和 transitions 必须已分配并填充）
 * @return 操作结果错误码
 */
fsm_err_t fsm_init(fsm_t* ctx, fsm_state_t initial_state,
    const fsm_config_t* config);

/**
 * @brief 执行状态机的一步
 *
 * 处理状态进入/退出回调，然后执行当前状态处理器。
 * 如果处理器返回了不同的状态，将校验转换规则后切换状态。
 *
 * @param ctx 状态机上下文指针
 * @return 操作结果错误码
 */
fsm_err_t fsm_step(fsm_t* ctx);

/**
 * @brief 请求外部状态转换
 *
 * 从外部强制触发状态切换，需通过转换矩阵校验。
 *
 * @param ctx 状态机上下文指针
 * @param target_state 目标状态
 * @return 操作结果错误码
 */
fsm_err_t fsm_goto(fsm_t* ctx, fsm_state_t target_state);

/**
 * @brief 填充转换矩阵，使所有状态之间可相互转换
 *
 * 将整个 state_count × state_count 的转换矩阵填充为同一个条件函数，
 * 实现全连通状态图。初始化前调用，通常在 fsm_init 之前。
 * 如需个别边使用不同条件，填充后再用 fsm_at() 覆盖。
 *
 * @param config 配置结构体指针
 * @param condition 转换条件函数（NULL = 无条件转换）
 */
void fsm_fill(fsm_config_t* config, fsm_guard_t condition);

/**
 * @brief 获取状态名称字符串
 * @param ctx 状态机上下文指针
 * @param state 状态ID
 * @return 状态名称字符串，未知则返回"UNKNOWN"
 */
const char* fsm_name(const fsm_t* ctx, fsm_state_t state);

/**
 * @brief 获取当前状态
 * @param ctx 状态机上下文指针
 * @return 当前状态ID
 */
static inline fsm_state_t fsm_current_state(const fsm_t* ctx)
{
    return ctx ? ctx->current_state : 0;
}

/**
 * @brief 获取用户数据指针
 * @param ctx 状态机上下文指针
 * @return 用户数据指针，失败返回NULL
 */
static inline void* fsm_user_data(const fsm_t* ctx)
{
    return ctx ? ctx->config.user_data : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* __FSM_H */