/**
 * @file    clist.c
 * @brief   嵌入式平台通用侵入式双向循环链表 —— 函数实现
 *
 * 本文件实现 clist.h 中声明的非内联函数：
 *   - clist_find()   ：节点查找
 *   - clist_get()    ：按索引取节点
 *   - clist_reverse()：链表反转
 *   - clist_sort()   ：选择排序
 *   - clist_purge()  ：清空链表
 *   - clist_dump()   ：调试打印
 *
 * 依赖：
 *   - clist.h（核心数据结构与内联操作）
 *   - stdio.h（仅 clist_dump 使用，若不需要可通过宏屏蔽）
 *
 * 编译示例（GCC/ARM-GCC）：
 *   gcc -Wall -Wextra -std=c99 -o test clist.c test_clist.c
 */

#include "clist.h"

#define CLIST_NO_PRINTF
/* --------------------------------------------------------------------------
 * 使用项目 debug 模块输出，通过 DEBUG_LOGD("clist", ...) 路由到 UART DMA FIFO。
 * 定义 CLIST_NO_PRINTF 可完全禁用 clist_dump()。
 * -------------------------------------------------------------------------- */
#ifndef CLIST_NO_PRINTF
#define CLIST_PRINTF(fmt, ...) DEBUG_LOGI("", fmt, ##__VA_ARGS__)
#else
#define CLIST_PRINTF(fmt, ...) /* 空实现：平台无标准输出时屏蔽 */
#endif

/**
 * @brief 在链表中线性查找与 target 地址相同的节点
 *
 * 算法：从头节点的 next 出发，依次比较节点指针直到回到头节点。
 * 时间复杂度：O(n)。
 *
 * @param head   链表头指针（不作为数据节点，仅用于确定遍历边界）
 * @param target 目标节点指针
 * @return       找到时返回 target；未找到返回 NULL
 */
clist_head_t* clist_find(const clist_head_t* head,
    const clist_head_t* target)
{
    const clist_head_t* pos;

    /* 参数合法性检查 */
    if (head == NULL || target == NULL) {
        return NULL;
    }

    /* 正向遍历，逐一比较节点指针 */
    for (pos = head->next; pos != head; pos = pos->next) {
        if (pos == target) {
            /* 找到目标节点，去掉 const 修饰后返回 */
            return (clist_head_t*)pos;
        }
    }

    return NULL; /* 未找到 */
}

/**
 * @brief 获取链表中第 index 个数据节点（0-based 索引）
 *
 * 算法：从 head->next 开始计数，走 index 步后返回当前节点。
 * 时间复杂度：O(n)，n 为 index 值。
 *
 * @param head  链表头指针
 * @param index 目标索引（0 = 第一个数据节点，1 = 第二个……）
 * @return      对应节点指针；若 index 超出链表长度则返回 NULL
 */
clist_head_t* clist_get(const clist_head_t* head, unsigned int index)
{
    const clist_head_t* pos;
    unsigned int i;

    if (head == NULL) {
        return NULL;
    }

    pos = head->next;
    for (i = 0; i < index; i++) {
        if (pos == head) {
            /* 还没走到第 index 步就已经回到头节点，说明越界 */
            return NULL;
        }
        pos = pos->next;
    }

    /* 再次检查：index 恰好等于链表长度时 pos 会等于 head */
    if (pos == head) {
        return NULL;
    }

    return (clist_head_t*)pos;
}

/**
 * @brief 就地反转链表中所有节点的顺序
 *
 * 算法：
 *   1. 遍历包含头节点在内的每一个节点；
 *   2. 交换每个节点的 prev 与 next 指针；
 *   3. 全部交换完成后，链表顺序即颠倒。
 *
 * 示意（3个数据节点）：
 *   反转前：head <-> A <-> B <-> C <-> head（循环）
 *   反转后：head <-> C <-> B <-> A <-> head（循环）
 *
 * 时间复杂度：O(n)。
 *
 * @param head 链表头指针
 */
void clist_reverse(clist_head_t* head)
{
    clist_head_t* cur;
    clist_head_t* tmp;

    if (head == NULL) {
        return;
    }

    cur = head;
    do {
        /* 交换 cur 节点的 prev 与 next */
        tmp = cur->next;
        cur->next = cur->prev;
        cur->prev = tmp;

        /* 继续前进（注意：此时 cur->prev 才是原来的 next）*/
        cur = cur->prev;
    } while (cur != head);
}

/**
 * @brief 【内部辅助】在 [start, head) 范围内找到 compare 最小的节点
 *
 * @param start   搜索起始节点
 * @param head    链表头（边界）
 * @param compare 比较函数（与 clist_sort 相同语义）
 * @return        最小节点指针
 */
static clist_head_t* __clist_find_min(clist_head_t* start,
    const clist_head_t* head,
    int (*compare)(const clist_head_t* a,
        const clist_head_t* b))
{
    clist_head_t* min_node = start;
    clist_head_t* pos;

    for (pos = start->next; pos != head; pos = pos->next) {
        if (compare(pos, min_node) < 0) {
            min_node = pos;
        }
    }
    return min_node;
}

/**
 * @brief 【内部辅助】交换链表中两个节点的位置
 *
 * 注意：a 必须在 b 的前面（a 先于 b 被遍历到）。
 * 处理了 a 与 b 相邻和不相邻两种情形。
 *
 * @param a 前面的节点
 * @param b 后面的节点
 */
static void __clist_swap(clist_head_t* a, clist_head_t* b)
{
    clist_head_t *a_prev, *a_next;
    clist_head_t *b_prev, *b_next;

    if (a == b) {
        return; /* 相同节点无需交换 */
    }

    a_prev = a->prev;
    a_next = a->next;
    b_prev = b->prev;
    b_next = b->next;

    if (a_next == b) {
        /* a 与 b 直接相邻：a <-> b */
        a_prev->next = b;
        b->prev = a_prev;
        b->next = a;
        a->prev = b;
        a->next = b_next;
        b_next->prev = a;
    } else {
        /* a 与 b 不相邻 */
        /* 将 b 插入到 a 原来的位置 */
        a_prev->next = b;
        b->prev = a_prev;
        b->next = a_next;
        a_next->prev = b;

        /* 将 a 插入到 b 原来的位置 */
        b_prev->next = a;
        a->prev = b_prev;
        a->next = b_next;
        b_next->prev = a;
    }
}

/**
 * @brief 使用选择排序对链表进行升序排列
 *
 * 算法：
 *   每轮从未排序区间 [cur, tail) 中找出最小节点，
 *   将其与 cur 位置的节点交换，然后 cur 后移一位，
 *   重复直到未排序区间只剩一个节点。
 *
 * 时间复杂度：O(n²)，空间复杂度：O(1)（原地排序，不额外分配内存）。
 * 适合嵌入式环境，节点数量通常较少（< 几百个）。
 *
 * @param head    链表头指针
 * @param compare 比较函数：
 *                  compare(a, b) < 0 → a 排在 b 前面（a 较小）
 *                  compare(a, b) = 0 → a 与 b 相等
 *                  compare(a, b) > 0 → b 排在 a 前面（b 较小）
 */
void clist_sort(clist_head_t* head,
    int (*compare)(const clist_head_t* a,
        const clist_head_t* b))
{
    clist_head_t* cur;
    clist_head_t* min_node;

    if (head == NULL || compare == NULL) {
        return;
    }

    /* 链表为空或只有一个节点时无需排序 */
    if (clist_empty(head) || clist_is_singular(head)) {
        return;
    }

    /* 选择排序主循环：cur 为每轮未排序区的起始节点 */
    for (cur = head->next; cur->next != head; cur = cur->next) {
        /* 在 [cur, head) 范围内寻找最小节点 */
        min_node = __clist_find_min(cur, head, compare);

        if (min_node != cur) {
            /* 将最小节点与 cur 交换位置 */
            __clist_swap(cur, min_node);

            /*
             * 交换后 min_node 占据了 cur 原来的位置，
             * 而 cur 被移走了，需要让循环变量指向新位置上的节点，
             * 即 min_node（它现在处于已排序区末尾）。
             */
            cur = min_node;
        }
    }
}

/**
 * @brief 逐个摘除链表中所有数据节点，并可选地释放宿主结构体内存
 *
 * 算法：
 *   反复从链表头摘除第一个节点，调用 free_fn 后继续，
 *   直到链表为空。
 *
 * 典型用法（宿主结构体通过 malloc 动态分配）：
 *
 *   void my_free(clist_head_t *node) {
 *       my_item_t *item = clist_entry(node, my_item_t, list_node);
 *       free(item);
 *   }
 *   clist_purge(&my_list, my_free);
 *
 * 典型用法（静态/栈分配，仅需摘除，不释放内存）：
 *   clist_purge(&my_list, NULL);
 *
 * @param head    链表头指针
 * @param free_fn 节点释放回调，传 NULL 则只摘除节点不做内存释放
 */
void clist_purge(clist_head_t* head,
    void (*free_fn)(clist_head_t* node))
{
    clist_head_t* node;
    clist_head_t* tmp;

    if (head == NULL) {
        return;
    }

    /* 安全遍历：先缓存下一节点，再删除当前节点 */
    node = head->next;
    while (node != head) {
        tmp = node->next; /* 缓存后继节点 */
        clist_del(node); /* 摘除当前节点（node->next/prev 置 NULL）*/
        if (free_fn != NULL) {
            free_fn(node); /* 用户自定义释放逻辑 */
        }
        node = tmp; /* 前进到下一节点 */
    }

    /* 最终重新初始化头节点，确保链表处于干净的空状态 */
    clist_init(head);
}

/**
 * @brief 以十六进制地址形式打印链表中所有节点（调试专用）
 *
 * 输出格式：
 *   [label] list @0xXXXX: 3 nodes
 *   [0] 0xAAAA  (prev=0xHEAD next=0xBBBB)
 *   [1] 0xBBBB  (prev=0xAAAA next=0xCCCC)
 *   [2] 0xCCCC  (prev=0xBBBB next=0xHEAD)
 *
 * 若目标平台无 printf，可将 CLIST_PRINTF 替换为 UART 打印函数。
 * 若完全不需要此功能，编译时加 -DCLIST_NO_PRINTF 即可排除。
 *
 * @param head  链表头指针
 * @param label 打印前缀标签（传 NULL 时使用默认标签 "clist"）
 */
void clist_dump(const clist_head_t* head, const char* label)
{
#ifndef CLIST_NO_PRINTF
    const clist_head_t* pos;
    unsigned int idx = 0;
    unsigned int total;

    if (head == NULL) {
        CLIST_PRINTF("[clist_dump] head is NULL\n");
        return;
    }

    if (label == NULL) {
        label = "clist";
    }

    /* 先统计总数 */
    total = clist_size(head);

    CLIST_PRINTF("[%s] list @%p: %u node(s)\n", label, (const void*)head, total);

    if (total == 0) {
        CLIST_PRINTF("  (empty)\n");
        return;
    }

    /* 逐节点打印地址及前驱/后继信息 */
    for (pos = head->next; pos != head; pos = pos->next, idx++) {
        CLIST_PRINTF("  [%u] %p  (prev=%p  next=%p)\n",
            idx,
            (const void*)pos,
            (const void*)pos->prev,
            (const void*)pos->next);
    }
#else
    /* 平台禁用 printf 时，此函数为空操作 */
    (void)head;
    (void)label;
#endif /* CLIST_NO_PRINTF */
}