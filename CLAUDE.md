# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

- **Toolchain**: `arm-none-eabi-gcc` (default preset). STM32CubeIDE wraps cmake via `cube-cmake` and also supports `starm-clang` (see `cmake/starm-clang.cmake`).
- **Generator**: Ninja

```bash
cmake --preset Debug
cmake --build build/Debug
```

A `Release` preset is also available (`-Os -g0`). Post-build, `.hex` and `.bin` files are generated via `arm-none-eabi-objcopy`. `compile_commands.json` is exported to `build/Debug/` for clangd (configured in `.clangd`).

## Project structure

| Directory | Ownership | Notes |
|---|---|---|
| `Core/` | CubeMX **generated** | HAL init, `main.c`, peripheral config (`gpio.c`, `fdcan.c`, `spi.c`, etc.). Contains `USER CODE BEGIN`/`END` guards. |
| `Drivers/` | **Vendor** (ST) | HAL + CMSIS. Do not modify. |
| `cmake/stm32cubemx/` | CubeMX **generated** | CMake config for HAL sources. Regenerated with CubeMX. |
| `tasks/` | **User** | Application glue layer. `app_main` lives here; each task owns `sw_timer` instances and wires drivers to services. |
| `device_drivers/` | **User** | Hardware abstraction layer (CAN, UART, Flash, LED, systick). Direct HAL usage lives here. |
| `service/` | **User** | Domain logic layer (currently `led` FSM). No HAL calls — receives callbacks for hardware access. |
| `m_middlewares/` | **User** | Reusable frameworks (`sw_timer`, `fsm`, `event`, `daemon`, `msg_fifo`, `kfifo`, `clist`, `log`, `protocol_packer`/`parser`, `key_base`) and algorithms (PID, gimbal PID, MIT control, filters, math, PLL, CRC). |
| `m_middlewares/public.h` | **User** | Central include umbrella — application code includes only this to get all middleware headers. |
| `stm32_g474_boot.ioc` | CubeMX **config** | Source of truth for pin mux, clocks, and peripheral assignment. |

## Architecture

### Boot sequence

```
Reset_Handler (startup_stm32g474xx.s)
  → SystemInit()
  → Copy .data to RAM, zero .bss
  → main()                          [Core/Src/main.c]
       → HAL_Init()
       → SystemClock_Config()       (160 MHz from HSE + PLL)
       → MX_GPIO/DMA/UART/SPI/FDCAN/TIM_Init()
       → app_main()                 [tasks/app_main.c]
            → delay_init()
            → log_task_init()
            → led_task_init()
            → while(1) { sw_timer_task() }
```

There is **no RTOS**. All periodic work runs through `sw_timer` — a cooperative scheduler driven by `sw_timer_task()` in the main loop. SysTick ISR calls `sw_timer_tick()` to mark expired timers ready; the main loop dispatches their callbacks by priority (HIGH → NORMAL → LOW). Adding periodic work always means creating a new `sw_timer`.

### Four-layer architecture

```
tasks/          → Glue: owns sw_timers, wires drivers to services
service/        → Domain logic (e.g. LED FSM with async command queue)
device_drivers/ → HAL calls, DMA, interrupts, kfifo buffering
m_middlewares/  → Reusable frameworks and algorithms
```

Tasks are thin — they own timers and stitch layers together. Services contain business logic but never touch HAL directly (they receive callback function pointers instead). Device drivers wrap HAL and expose `init()`/`deinit()`/`is_initialized()` lifecycles.

### Middleware ecosystem

- **`sw_timer`** — Software timers with priority levels. Single-shot, N-repeat, or infinite. Backbone of all periodic work.
- **`fsm`** — Flat state machine with guard-matrix transition validation. Used by `service/led`. Not included in `public.h` (include manually).
- **`event`** — ISR-safe 32-bit event flags for main-loop polling.
- **`daemon`** — Task watchdog with online/offline callbacks and debounce.
- **`kfifo`** — Lock-free power-of-2 ring buffer. Single-producer/single-consumer safe from ISR. Used for all DMA buffering.
- **`clist`** — Intrusive circular doubly-linked list (Linux-kernel style). Backbone of `daemon`, `sw_timer`, `key_base` instance registries.
- **`msg_fifo`** — Typed message queue layered on `kfifo`.
- **`log`** — ESP32-style colored logging (`LOG_E`/`W`/`I`/`D`/`T` macros). Output buffered through a `kfifo` for non-blocking DMA TX.
- **`protocol_packer`/`protocol_parser`** — Stateless frame builder / stateful frame de-serializer with configurable header/footer/checksum callbacks.
- **`key_base`** — Button debounce with multi-event detection (click, double-click, long press, etc.).
- **Algorithms**: PID (position/incremental), gimbal PID (cascade + gyro feedforward), MIT impedance control, PT1/Biquad/Slew/LMA filters, fast trig (`sin_approx`, `atan2_approx`), median filters, software PLL, CRC8/CRC16.

### Bootloader design

The project implements a **dual A/B partition** firmware upgrade system over CAN/CAN FD (see `boot_design.md`). The protocol uses:
- CAN IDs `0x701` (Host→Node) and `0x702` (Node→Host)
- 2-byte header (Command + Sequence) per frame
- 1KB block checksums with checksum at fixed offset (Byte 2-3) to avoid CAN FD padding issues
- Commands: START, METADATA, DATA, DATA_END, VERIFY, REBOOT, ACK/NACK
- Support for both Classic CAN (8-byte frames) and CAN FD (up to 64-byte frames), negotiated at START

## Coding conventions

Follow **`MODULE_CODING_GUIDE.md`** for all new code. Key rules:

- **Style**: WebKit (4-space indent, Allman braces for functions, K&R braces for `if`/`for`/`while`). Max 100 chars per line.
- **Standard**: C11 (`-std=gnu11`), MISRA C:2012 compliance expected.
- **Naming**: `snake_case` with module prefix. Types end in `_t` (e.g. `protocol_parser_config_t`). Error enums start with `MODULE_OK = 0`. Static globals use `s_` prefix.
- **config-in-context pattern**: Every module has a `xxx_config_t` (immutable parameters + callbacks) nested inside a `xxx_context_t` (runtime state + copy of config + `initialized` flag). Init copies config, sets `initialized = true`. All public functions guard with `if (!ctx->initialized) return ERROR_UNINITIALIZED`.
- **Static allocation**: No `malloc` in middleware or services. Callers provide memory; modules use `xxx_register_static()` patterns.
- **Comments**: Chinese Doxygen-style (`@brief`/`@param`/`@return`) for public API. Source comments are mixed Chinese (GBK in CubeMX files, UTF-8 in user files).
- **Types**: Always use fixed-width integers (`uint8_t`, `uint16_t`, `uint32_t`) and `bool` from `<stdbool.h>`.
- **Section ordering**: `/* Includes */` → `/* Private constants */` → `/* Private variables */` → `/* Private function prototypes */` → `/* Exported functions */` → `/* Private functions */`.

## CubeMX code regeneration

Re-generating from the `.ioc` file **overwrites** `Core/` and `cmake/stm32cubemx/`. Code inside `/* USER CODE BEGIN ... */` / `/* USER CODE END ... */` guards in `Core/Src/main.c` is preserved; everything outside is lost. Only add code between the guards in CubeMX-managed files.

## Hardware

- **MCU**: STM32G474RBTx — Cortex-M4 with FPU, 128KB Flash (`0x08000000`), 128KB RAM (`0x20000000`)
- **Clock**: 160 MHz from HSE + PLL (Voltage Scale 1 + Boost, Flash latency 4)
- **Linker**: `STM32G474XX_FLASH.ld` — heap 512B, stack 1024B, newlib-nano, `--gc-sections`
- **Toolchain flags**: `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`
- **Peripherals**: SPI1, FDCAN1 (PA11/PA12), FDCAN2 (PB12/PB13), USART1 (DMA TX + IDLE-line RX), TIM1, TIM15, GPIO, DMA
- No tests, no CI, no formatter/linter configured.
