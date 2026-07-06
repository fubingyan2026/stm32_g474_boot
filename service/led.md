# LED 控制模块 (led)

<p align='right'>版本: 2.1.0 | 作者: max </p>

支持常亮、熄灭、编码闪烁、呼吸灯四种工作状态的 LED 控制模块。采用 **FSM 状态机** 管理状态转换，**msg_fifo 异步命令队列** 接收外部指令，**clist 侵入式链表** 管理实例。

---

## 核心特性

- **异步命令架构** — 内置 `msg_fifo` 命令队列，状态切换和参数修改均为异步执行，不阻塞调用者。
- **FSM 状态管理** — ON/OFF/BLINK_CODE/BREATHING，支持进入/退出回调。
- **硬件解耦** — 引脚操作通过 `write_pin(uint16_t)` 回调注入，模块不直接依赖 HAL。
- **闪烁参数热更新** — 闪烁过程中可动态修改频率/次数，亮着时延迟到熄灭后生效，避免毛刺。
- **呼吸灯** — 基于正弦 + Gamma 校正的平滑呼吸曲线，支持动态调整周期/亮度范围。
- **静态分配** — 所有内存由调用者提供，无动态分配依赖。

---

## 配置结构体

### `led_config_t` — LED 基础配置

| 字段               | 类型                        | 说明                                  |
| :----------------- | :-------------------------- | :------------------------------------ |
| `name`             | `const char*`               | LED 唯一名称，用于查找                |
| `init_state`       | `led_state_t`               | 注册后的初始状态                      |
| `write_pin`        | `void (*)(uint16_t value)`  | 引脚写入：0=灭, 1023=最亮, 中间=呼吸 |
| `breath_cycle_ms`  | `uint16_t`                  | 呼吸周期(ms)，0=2000                  |
| `breath_step_ms`   | `uint16_t`                  | 步进间隔(ms)，0=30                    |
| `breath_min_duty`  | `uint16_t`                  | 呼吸最小亮度(0-1023)，0=0             |
| `breath_max_duty`  | `uint16_t`                  | 呼吸最大亮度(0-1023)，0=1023          |

写入回调用户自行实现，负责 PWM 输出或 GPIO 写（0 和 1023 对应完全灭和亮）。

### `led_cmd_t` — 异步命令

| 字段                    | 类型          | 说明                            |
| :---------------------- | :------------ | :------------------------------ |
| `led_set_state`         | `led_state_t` | 目标状态                        |
| `led_blink_cycle_ms`    | `uint16_t`    | 闪烁间隔 (ms)                   |
| `led_blink_wait_ms`     | `uint16_t`    | 等待间隔 (ms)                   |
| `led_blink_code_counts` | `uint16_t`    | 闪烁次数（`0`=无限循环）        |
| `led_breath_cycle_ms`   | `uint16_t`    | 呼吸周期(ms)，0=不变            |
| `led_breath_min_duty`   | `uint16_t`    | 最小亮度，`0xFFFF`=不变         |
| `led_breath_max_duty`   | `uint16_t`    | 最大亮度，`0xFFFF`=不变         |

---

## 使用指南

### 1. 初始化系统

```c
#include "led.h"

// 注入系统毫秒计数函数
led_init(HAL_GetTick);
```

### 2. 实现引脚回调

```c
// PWM 模式（推荐）
static void led_write_pin(uint16_t value) {
    // value 0-1023，设置 PWM 占空比或 GPIO
    pwm_set_duty(value);
}
```

### 3. 注册 LED 实例

```c
static led_handle_t s_led;

const led_config_t cfg = {
    .name = "LED0",
    .init_state = LED_STATE_OFF,
    .write_pin = led_write_pin,
    .breath_cycle_ms = 2000,   // 2s 呼吸周期
    .breath_min_duty = 0,
    .breath_max_duty = 1023,
};
led_register_static(&cfg, &s_led);
```

### 4. 注册回调

```c
void on_state_change(led_handle_t* h, led_state_t s, void* ud) { ... }
void on_blink_phase(led_handle_t* h, led_blink_phase_t p, void* ud) { ... }
void on_edge(led_handle_t* h, bool rising, void* ud) { ... }

led_set_callbacks(led, on_state_change, on_blink_phase, on_edge, NULL);
```

### 5. 控制 LED

```c
led_set_state(led, LED_STATE_ON);              // 常亮 (write_pin(1023))
led_set_state(led, LED_STATE_OFF);             // 熄灭 (write_pin(0))
led_set_state(led, LED_STATE_BLINK_CODE);      // 编码闪烁
led_set_state(led, LED_STATE_BREATHING);       // 呼吸灯
```

**配置闪烁参数（异步命令）:**

```c
led_set_blink_interval(led, &(led_cmd_t){
    .led_blink_cycle_ms = 100,
    .led_blink_wait_ms = 1000,
    .led_blink_code_counts = 3,
});
led_set_state(led, LED_STATE_BLINK_CODE);
```

**配置呼吸参数（异步命令）:**

```c
led_set_state(led, &(led_cmd_t){
    .led_set_state = LED_STATE_BREATHING,
    .led_breath_cycle_ms = 3000,
    .led_breath_min_duty = 100,
    .led_breath_max_duty = 900,
});
```

### 6. 任务刷新

```c
// 需在主循环或定时器中定期调用（建议周期 ≤ 10ms）
led_task_refresh();
```

---

## 编码闪烁行为

闪烁由两个阶段构成循环：

- **BLINKING** — 按 `led_blink_cycle_ms` 间隔翻转（1023/0），每个下降沿计数一次
- **INTERVAL** — 保持熄灭 `led_blink_wait_ms`，结束后继续下一轮

| `led_blink_code_counts` | 行为 |
| :---------------------- | :--------------------------------------- |
| `0`                     | 无限循环，不会自动关闭 |
| `> 0`                   | 闪烁指定次数后自动切换到 `LED_STATE_OFF` |

---

## 呼吸灯行为

呼吸灯通过正弦波 + Gamma 校正实现平滑的自然呼吸效果：

```
phase = breath_cycle × 2π / total_steps
brightness = (sin(phase) + 1) × 0.5
gamma = powerf(brightness, 2.2)
output = min_duty + gamma × (max_duty - min_duty)
```

- 从其他状态切换到 BREATHING 时，自动从**当前亮度**反算初始相位，无跳变
- 可通过异步命令动态修改周期、亮度范围，实时生效

---

## 状态列表

| 状态                     | 说明       |
| :----------------------- | :--------- |
| `LED_STATE_NONE`         | 空闲       |
| `LED_STATE_OFF`          | 熄灭       |
| `LED_STATE_ON`           | 常亮       |
| `LED_STATE_BLINK_CODE`   | 编码闪烁   |
| `LED_STATE_BREATHING`    | 呼吸灯     |

### 状态转换图

```mermaid
stateDiagram-v2
    [*] --> NONE : 注册后初始状态

    NONE --> OFF : led_set_state()
    NONE --> ON : led_set_state()
    NONE --> BLINK_CODE : led_set_state()
    NONE --> BREATHING : led_set_state()

    OFF --> ON : led_set_state()
    OFF --> BLINK_CODE : led_set_state()
    OFF --> BREATHING : led_set_state()

    ON --> OFF : led_set_state()
    ON --> BLINK_CODE : led_set_state()
    ON --> BREATHING : led_set_state()

    BLINK_CODE --> OFF : 闪烁计数完成\n(自动转换)
    BLINK_CODE --> ON : led_set_state()
    BLINK_CODE --> BREATHING : led_set_state()

    BREATHING --> OFF : led_set_state()
    BREATHING --> ON : led_set_state()
    BREATHING --> BLINK_CODE : led_set_state()
```

> - 实线箭头：外部通过 `led_set_state()` 触发的主动切换
> - 虚线箭头：FSM 内部自动转换（BLINK_CODE 计数完成后切 OFF，其他状态无自动转换）
> - 所有状态均可互相转换，FSM 未限制任何转移路径

---

## API 参考

| 函数                             | 说明                                    |
| :------------------------------- | :-------------------------------------- |
| `led_init(cb)`                   | 初始化 LED 子系统，注入时间回调         |
| `led_deinit()`                   | 反初始化，释放所有资源                  |
| `led_register_static(cfg, h)`    | 静态注册 LED，使用预分配内存            |
| `led_unregister(name)`           | 注销指定名称的 LED                      |
| `led_get_instance(name)`         | 根据名称获取 LED 句柄                   |
| `led_get_head()`                 | 获取 LED 链表头                         |
| `led_set_state(h, state)`        | 异步设置 LED 运行状态                   |
| `led_set_blink_interval(h, cmd)` | 异步配置闪烁参数                        |
| `led_get_blink_phase(h)`         | 获取当前闪烁阶段                        |
| `led_set_callbacks(...)`         | 注册状态/闪烁阶段/边沿回调              |
| `led_task_refresh()`             | **核心任务**，驱动所有 LED FSM 状态步进 |

---

## 注意事项

1. **刷新频率**: `led_task_refresh()` 的调用频率决定了闪烁/呼吸的精度，建议周期 ≤ 10ms。
2. **引脚初始化**: `led_register_static` 前需要调用者自行完成 GPIO/PWM 初始化。
3. **队列容量**: 命令队列约可缓存 9 条命令，超过容量时旧命令不会被覆盖、新命令静默丢弃。
4. **write_pin 范围**: 值域 0-1023。0=完全熄灭，1023=最亮，中间值用于呼吸 PWM。
5. **呼吸平滑过渡**: 从 ON（1023）或 OFF（0）切换到 BREATHING 时，从当前亮度开始连续呼吸，无突变。
