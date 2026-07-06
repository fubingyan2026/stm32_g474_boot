/**
 * @file    clist.h
 * @brief   嵌入式平台通用侵入式双向循环链表
 *
 * 设计思路（与 Linux kernel list.h 相同）：
 *   链表节点 clist_head 直接嵌入宿主结构体，不单独分配节点内存。
 *   通过 clist_entry() 宏，由节点指针反向计算出宿主结构体指针。
 *   这种"侵入式"设计无需动态内存分配，非常适合嵌入式环境。
 *
 * 使用示例：
 *   typedef struct {
 *       int          value;
 *       clist_head_t node;   // 嵌入链表节点
 *   } my_item_t;
 *
 *   CLIST_HEAD(my_list);                         // 定义并初始化链表头
 *   my_item_t item = { .value = 42 };
 *   clist_add_tail(&my_list, &item.node);        // 尾插
 *
 *   clist_head_t *pos;
 *   clist_for_each(pos, &my_list) {              // 遍历
 *       my_item_t *p = clist_entry(pos, my_item_t, node);
 *       // 使用 p->value ...
 *   }
 *
 * @note    本文件仅使用 stddef.h（提供 offsetof/NULL），
 *          不依赖动态内存，可在裸机环境直接使用。
 */

#ifndef __CLIST_H__
#define __CLIST_H__

#include <stddef.h> /* offsetof, NULL */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 双向循环链表节点
 *
 * 将此结构体嵌入到任意宿主结构体中，即可让宿主结构体参与链表操作。
 * - prev：指向前一个节点
 * - next：指向后一个节点
 * 链表头本身也是一个 clist_head_t，形成循环：
 *   空链表时 head.prev == head.next == &head
 */
typedef struct clist_head {
    struct clist_head* prev; /**< 指向前驱节点 */
    struct clist_head* next; /**< 指向后继节点 */
} clist_head_t;

/**
 * @brief 由结构体成员指针反推宿主结构体指针（与 Linux container_of 相同）
 *
 * @param ptr    指向结构体成员的指针
 * @param type   宿主结构体类型
 * @param member 成员在宿主结构体中的字段名
 * @return       指向宿主结构体的指针
 *
 * 原理：成员地址 - 成员在结构体内的偏移量 = 结构体首地址
 */
#ifndef container_of
#define container_of(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))
#endif

/**
 * @brief 由链表节点指针取得宿主结构体指针
 *
 * @param ptr    clist_head_t* 节点指针
 * @param type   宿主结构体类型
 * @param member 宿主结构体中 clist_head_t 字段的名称
 * @return       指向宿主结构体的指针
 *
 * 示例：my_item_t *item = clist_entry(pos, my_item_t, node);
 */
#define clist_entry(ptr, type, member) \
    container_of(ptr, type, member)

/**
 * @brief 取链表第一个数据节点对应的宿主结构体
 *
 * @param head   链表头指针（clist_head_t*）
 * @param type   宿主结构体类型
 * @param member 宿主结构体中链表节点字段名
 * @note  链表为空时行为未定义，调用前应先用 clist_empty() 检查
 */
#define clist_first_entry(head, type, member) \
    clist_entry((head)->next, type, member)

/**
 * @brief 取链表最后一个数据节点对应的宿主结构体
 *
 * @param head   链表头指针（clist_head_t*）
 * @param type   宿主结构体类型
 * @param member 宿主结构体中链表节点字段名
 * @note  链表为空时行为未定义，调用前应先用 clist_empty() 检查
 */
#define clist_last_entry(head, type, member) \
    clist_entry((head)->prev, type, member)

/**
 * @brief 取当前节点的下一个宿主结构体指针
 *
 * @param pos    当前宿主结构体指针
 * @param member 宿主结构体中链表节点字段名
 */
#define clist_next_entry(pos, member) \
    clist_entry((pos)->member.next, typeof(*(pos)), member)

/**
 * @brief 取当前节点的上一个宿主结构体指针
 *
 * @param pos    当前宿主结构体指针
 * @param member 宿主结构体中链表节点字段名
 */
#define clist_prev_entry(pos, member) \
    clist_entry((pos)->member.prev, typeof(*(pos)), member)

/**
 * @brief 静态初始化链表头的初始值（用于结构体/全局变量初始化）
 *
 * 示例：
 *   clist_head_t my_list = CLIST_HEAD_INIT(my_list);
 */
#define CLIST_HEAD_INIT(name) { &(name), &(name) }

/**
 * @brief 定义并静态初始化一个链表头变量
 *
 * 示例：
 *   CLIST_HEAD(my_list);   // 等价于: clist_head_t my_list = CLIST_HEAD_INIT(my_list);
 */
#define CLIST_HEAD(name) \
    clist_head_t name = CLIST_HEAD_INIT(name)

/**
 * @brief 运行时初始化链表头
 *
 * 当链表头是动态分配或嵌套在其他结构体内部时，使用此宏进行初始化。
 * 初始化后 prev 和 next 均指向自身，表示空链表。
 *
 * @param head 指向 clist_head_t 的指针
 */
#define CLIST_HEAD_INIT_PTR(head) \
    do {                          \
        (head)->next = (head);    \
        (head)->prev = (head);    \
    } while (0)

/**
 * @brief 运行时初始化链表头（函数版本，语义更清晰）
 *
 * @param head 链表头指针
 */
static inline void clist_init(clist_head_t* head)
{
    head->next = head;
    head->prev = head;
}

/**
 * @brief 判断链表是否为空
 *
 * 空链表条件：head->next == head（自身成环）
 *
 * @param head 链表头指针
 * @return     非零（真）表示链表为空，0 表示链表非空
 */
static inline int clist_empty(const clist_head_t* head)
{
    return head->next == head;
}

/**
 * @brief 判断链表是否只有一个数据节点
 *
 * 条件：head->next != head 且 head->next == head->prev
 *
 * @param head 链表头指针
 * @return     非零（真）表示只有一个节点
 */
static inline int clist_is_singular(const clist_head_t* head)
{
    return !clist_empty(head) && (head->next == head->prev);
}

/**
 * @brief 获取链表节点数量（O(n) 遍历计数）
 *
 * @param head 链表头指针
 * @return     链表中数据节点个数（不含头节点）
 * @note  时间复杂度 O(n)，嵌入式环境中谨慎在高频路径调用
 */
static inline unsigned int clist_size(const clist_head_t* head)
{
    unsigned int count = 0;
    const clist_head_t* pos = head->next;
    while (pos != head) {
        count++;
        pos = pos->next;
    }
    return count;
}

/**
 * @brief 判断某个节点是否是链表的最后一个节点
 *
 * @param node 要判断的节点指针
 * @param head 链表头指针
 * @return     非零（真）表示是最后一个节点
 */
static inline int clist_is_last(const clist_head_t* node,
    const clist_head_t* head)
{
    return node->next == head;
}

/**
 * @brief 判断某个节点是否是链表的第一个节点
 *
 * @param node 要判断的节点指针
 * @param head 链表头指针
 * @return     非零（真）表示是第一个节点
 */
static inline int clist_is_first(const clist_head_t* node,
    const clist_head_t* head)
{
    return node->prev == head;
}

/**
 * @brief 【内部辅助】在 prev 和 next 之间插入新节点 new_node
 *
 * 调用者保证 prev->next == next && next->prev == prev
 *
 * @param new_node 要插入的新节点
 * @param prev     新节点的前驱
 * @param next     新节点的后继
 */
static inline void __clist_add(clist_head_t* new_node,
    clist_head_t* prev,
    clist_head_t* next)
{
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    prev->next = new_node;
}

/**
 * @brief 头插法：在链表头部（head 之后）插入节点
 *
 * 插入后，new_node 成为链表的第一个数据节点。
 * 适用于实现栈（LIFO）。
 *
 * @param head     链表头节点指针
 * @param new_node 要插入的新节点指针
 */
static inline void clist_add(clist_head_t* head, clist_head_t* new_node)
{
    __clist_add(new_node, head, head->next);
}

/**
 * @brief 尾插法：在链表尾部（head 之前）插入节点
 *
 * 插入后，new_node 成为链表的最后一个数据节点。
 * 适用于实现队列（FIFO）。
 *
 * @param head     链表头节点指针
 * @param new_node 要插入的新节点指针
 */
static inline void clist_add_tail(clist_head_t* head, clist_head_t* new_node)
{
    __clist_add(new_node, head->prev, head);
}

/**
 * @brief 在指定节点 pos 之后插入新节点
 *
 * @param pos      已在链表中的参考节点
 * @param new_node 要插入的新节点
 */
static inline void clist_add_after(clist_head_t* pos, clist_head_t* new_node)
{
    __clist_add(new_node, pos, pos->next);
}

/**
 * @brief 在指定节点 pos 之前插入新节点
 *
 * @param pos      已在链表中的参考节点
 * @param new_node 要插入的新节点
 */
static inline void clist_add_before(clist_head_t* pos, clist_head_t* new_node)
{
    __clist_add(new_node, pos->prev, pos);
}

/**
 * @brief 【内部辅助】将 prev 与 next 直接相连，摘除中间节点
 *
 * @param prev 被删节点的前驱
 * @param next 被删节点的后继
 */
static inline void __clist_del(clist_head_t* prev, clist_head_t* next)
{
    next->prev = prev;
    prev->next = next;
}

/**
 * @brief 从链表中摘除节点 node
 *
 * 摘除后 node 的 prev/next 被置为 NULL，防止野指针误用。
 * 宿主结构体的内存本身不受影响（侵入式设计不管理内存）。
 *
 * @param node 要删除的节点指针
 * @note  删除后不可再对 node 调用链表操作，除非重新初始化
 */
static inline void clist_del(clist_head_t* node)
{
    __clist_del(node->prev, node->next);
    /* 置毒指针：帮助调试，防止悬空访问 */
    node->next = NULL;
    node->prev = NULL;
}

/**
 * @brief 从链表中摘除节点，并将节点重新初始化为空链表状态
 *
 * 与 clist_del() 的区别：摘除后节点仍可安全调用 clist_empty() 等操作。
 *
 * @param node 要删除并重置的节点指针
 */
static inline void clist_del_init(clist_head_t* node)
{
    __clist_del(node->prev, node->next);
    clist_init(node);
}

/**
 * @brief 删除链表头部第一个数据节点，并返回其指针
 *
 * @param head 链表头指针
 * @return     被删除的节点指针；若链表为空则返回 NULL
 */
static inline clist_head_t* clist_del_head(clist_head_t* head)
{
    clist_head_t* node;
    if (clist_empty(head)) {
        return NULL;
    }
    node = head->next;
    clist_del(node);
    return node;
}

/**
 * @brief 删除链表尾部最后一个数据节点，并返回其指针
 *
 * @param head 链表头指针
 * @return     被删除的节点指针；若链表为空则返回 NULL
 */
static inline clist_head_t* clist_del_tail(clist_head_t* head)
{
    clist_head_t* node;
    if (clist_empty(head)) {
        return NULL;
    }
    node = head->prev;
    clist_del(node);
    return node;
}

/**
 * @brief 用新节点替换链表中的旧节点
 *
 * 替换后 old_node 的 prev/next 被置为 NULL。
 * new_node 接管 old_node 在链表中的位置。
 *
 * @param old_node 链表中已存在的节点
 * @param new_node 用于替换的新节点
 */
static inline void clist_replace(clist_head_t* old_node,
    clist_head_t* new_node)
{
    new_node->next = old_node->next;
    new_node->next->prev = new_node;
    new_node->prev = old_node->prev;
    new_node->prev->next = new_node;
    /* 置毒，防止旧节点被误用 */
    old_node->next = NULL;
    old_node->prev = NULL;
}

/**
 * @brief 将节点 node 从当前链表移动到目标链表头部
 *
 * 等价于先 clist_del(node) 再 clist_add(head, node)，但操作原子语义更清晰。
 *
 * @param head 目标链表头指针
 * @param node 要移动的节点
 */
static inline void clist_move(clist_head_t* head, clist_head_t* node)
{
    __clist_del(node->prev, node->next);
    clist_add(head, node);
}

/**
 * @brief 将节点 node 从当前链表移动到目标链表尾部
 *
 * @param head 目标链表头指针
 * @param node 要移动的节点
 */
static inline void clist_move_tail(clist_head_t* head, clist_head_t* node)
{
    __clist_del(node->prev, node->next);
    clist_add_tail(head, node);
}

/**
 * @brief 将 list 链表中的所有节点拼接到 head 链表的头部
 *
 * 拼接后 list 被清空（重新初始化为空链表）。
 * 若 list 本身为空则不做任何操作。
 *
 * @param head 目标链表头（接收节点）
 * @param list 源链表头（提供节点，操作后变为空链表）
 */
static inline void clist_splice(clist_head_t* head, clist_head_t* list)
{
    if (!clist_empty(list)) {
        clist_head_t* first = list->next; /* list 的第一个节点 */
        clist_head_t* last = list->prev; /* list 的最后一个节点 */
        clist_head_t* at = head->next; /* head 当前的第一个节点 */

        first->prev = head;
        head->next = first;

        last->next = at;
        at->prev = last;

        clist_init(list); /* 清空源链表 */
    }
}

/**
 * @brief 将 list 链表中的所有节点拼接到 head 链表的尾部
 *
 * 拼接后 list 被清空（重新初始化为空链表）。
 *
 * @param head 目标链表头（接收节点）
 * @param list 源链表头（提供节点，操作后变为空链表）
 */
static inline void clist_splice_tail(clist_head_t* head, clist_head_t* list)
{
    if (!clist_empty(list)) {
        clist_head_t* first = list->next;
        clist_head_t* last = list->prev;
        clist_head_t* at = head->prev; /* head 当前的最后一个节点 */

        first->prev = at;
        at->next = first;

        last->next = head;
        head->prev = last;

        clist_init(list);
    }
}

/**
 * @brief 正向遍历链表（从头到尾）
 *
 * pos 为循环变量（clist_head_t*），指向当前节点。
 * 遍历期间不可删除当前节点，否则请使用 clist_for_each_safe。
 *
 * @param pos  循环变量，类型 clist_head_t*
 * @param head 链表头指针
 */
#define clist_for_each(pos, head) \
    for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

/**
 * @brief 反向遍历链表（从尾到头）
 *
 * @param pos  循环变量，类型 clist_head_t*
 * @param head 链表头指针
 */
#define clist_for_each_prev(pos, head) \
    for ((pos) = (head)->prev; (pos) != (head); (pos) = (pos)->prev)

/**
 * @brief 正向安全遍历链表（允许在遍历中删除当前节点）
 *
 * 使用临时变量 tmp 提前缓存下一个节点，因此删除 pos 后仍可继续遍历。
 *
 * @param pos  循环变量，类型 clist_head_t*
 * @param tmp  临时缓存变量，类型 clist_head_t*（用户自行声明）
 * @param head 链表头指针
 */
#define clist_for_each_safe(pos, tmp, head)         \
    for ((pos) = (head)->next, (tmp) = (pos)->next; \
        (pos) != (head);                            \
        (pos) = (tmp), (tmp) = (pos)->next)

/**
 * @brief 反向安全遍历链表（允许在遍历中删除当前节点）
 *
 * @param pos  循环变量，类型 clist_head_t*
 * @param tmp  临时缓存变量，类型 clist_head_t*
 * @param head 链表头指针
 */
#define clist_for_each_prev_safe(pos, tmp, head)    \
    for ((pos) = (head)->prev, (tmp) = (pos)->prev; \
        (pos) != (head);                            \
        (pos) = (tmp), (tmp) = (pos)->prev)

/**
 * @brief 正向遍历链表，pos 直接为宿主结构体指针
 *
 * 使用此宏可以省去循环体内的 clist_entry() 调用，代码更简洁。
 *
 * @param pos    循环变量，类型为宿主结构体指针（用户自行声明）
 * @param head   链表头指针（clist_head_t*）
 * @param member 宿主结构体中链表节点字段名
 *
 * 示例：
 *   my_item_t *pos;
 *   clist_for_each_entry(pos, &my_list, node) {
 *       printf("%d\n", pos->value);
 *   }
 */
#define clist_for_each_entry(pos, head, member)                     \
    for ((pos) = clist_entry((head)->next, typeof(*(pos)), member); \
        &(pos)->member != (head);                                   \
        (pos) = clist_entry((pos)->member.next, typeof(*(pos)), member))

/**
 * @brief 反向遍历链表，pos 直接为宿主结构体指针
 *
 * @param pos    循环变量，宿主结构体指针
 * @param head   链表头指针
 * @param member 宿主结构体中链表节点字段名
 */
#define clist_for_each_entry_reverse(pos, head, member)             \
    for ((pos) = clist_entry((head)->prev, typeof(*(pos)), member); \
        &(pos)->member != (head);                                   \
        (pos) = clist_entry((pos)->member.prev, typeof(*(pos)), member))

/**
 * @brief 安全正向遍历（宿主结构体指针版，允许删除当前节点）
 *
 * @param pos    循环变量，宿主结构体指针
 * @param tmp    临时缓存变量，与 pos 类型相同（用户自行声明）
 * @param head   链表头指针
 * @param member 宿主结构体中链表节点字段名
 */
#define clist_for_each_entry_safe(pos, tmp, head, member)                \
    for ((pos) = clist_entry((head)->next, typeof(*(pos)), member),      \
        (tmp) = clist_entry((pos)->member.next, typeof(*(pos)), member); \
        &(pos)->member != (head);                                        \
        (pos) = (tmp),                                                   \
        (tmp) = clist_entry((tmp)->member.next, typeof(*(tmp)), member))

/**
 * @brief 安全反向遍历（宿主结构体指针版，允许删除当前节点）
 *
 * @param pos    循环变量，宿主结构体指针
 * @param tmp    临时缓存变量，与 pos 类型相同
 * @param head   链表头指针
 * @param member 宿主结构体中链表节点字段名
 */
#define clist_for_each_entry_safe_reverse(pos, tmp, head, member)        \
    for ((pos) = clist_entry((head)->prev, typeof(*(pos)), member),      \
        (tmp) = clist_entry((pos)->member.prev, typeof(*(pos)), member); \
        &(pos)->member != (head);                                        \
        (pos) = (tmp),                                                   \
        (tmp) = clist_entry((tmp)->member.prev, typeof(*(tmp)), member))

/**
 * @brief 从指定节点 pos 开始继续正向遍历（断点续遍历）
 *
 * 适用于从某个已知节点开始继续向后遍历，而非从链表头开始。
 *
 * @param pos    起始宿主结构体指针（已在链表中）
 * @param head   链表头指针
 * @param member 宿主结构体中链表节点字段名
 */
#define clist_for_each_entry_continue(pos, head, member)                  \
    for ((pos) = clist_entry((pos)->member.next, typeof(*(pos)), member); \
        &(pos)->member != (head);                                         \
        (pos) = clist_entry((pos)->member.next, typeof(*(pos)), member))

/**
 * @brief 在链表中查找与目标节点相同的节点
 *
 * 线性遍历，时间复杂度 O(n)。
 *
 * @param head   链表头指针
 * @param target 目标节点指针
 * @return       找到时返回目标节点指针；未找到返回 NULL
 */
clist_head_t* clist_find(const clist_head_t* head,
    const clist_head_t* target);

/**
 * @brief 获取链表中第 index 个节点（0-based）
 *
 * @param head  链表头指针
 * @param index 索引值，从 0 开始
 * @return      对应节点指针；若越界则返回 NULL
 */
clist_head_t* clist_get(const clist_head_t* head, unsigned int index);

/**
 * @brief 将链表就地反转（首尾对调）
 *
 * @param head 链表头指针
 */
void clist_reverse(clist_head_t* head);

/**
 * @brief 用选择排序对链表按用户提供的比较函数升序排序
 *
 * 时间复杂度 O(n²)，适合嵌入式环境中节点数量较少的场景。
 *
 * @param head    链表头指针
 * @param compare 比较函数：
 *                  返回值 < 0 表示 a 应排在 b 前面
 *                  返回值 = 0 表示 a 与 b 相等
 *                  返回值 > 0 表示 a 应排在 b 后面
 */
void clist_sort(clist_head_t* head,
    int (*compare)(const clist_head_t* a,
        const clist_head_t* b));

/**
 * @brief 将整个链表清空（逐个摘除并重置所有节点）
 *
 * 每个被摘除的节点都会调用用户提供的 free_fn 回调，
 * 用户可在回调中释放宿主结构体内存（若使用动态分配）。
 *
 * @param head    链表头指针
 * @param free_fn 节点释放回调（可传 NULL 表示不释放内存，只摘除节点）
 */
void clist_purge(clist_head_t* head,
    void (*free_fn)(clist_head_t* node));

/**
 * @brief 打印链表所有节点地址（调试用途）
 *
 * @param head  链表头指针
 * @param label 打印前缀标签字符串（可传 NULL）
 */
void clist_dump(const clist_head_t* head, const char* label);

#ifdef __cplusplus
}
#endif

#endif /* __CLIST_H__ */