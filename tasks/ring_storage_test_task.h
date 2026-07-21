/**
 * @file    ring_storage_test_task.h
 * @brief   ring_storage 模块功能测试任务
 *
 * 上电自测试：验证 ring_storage 的 save / load / persistence / GC 功能。
 * 测试区域使用 Flash 末尾空闲区域（0x0801C000 ~ 0x0801DFFF，8KB）。
 */

#ifndef __RING_STORAGE_TEST_TASK_H
#define __RING_STORAGE_TEST_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief 运行 ring_storage 功能测试
 * @note  在 app_main() 中初始化后调用，测试结果输出到日志系统
 */
void ring_storage_test_task_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __RING_STORAGE_TEST_TASK_H */
