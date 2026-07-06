# AGENTS.md — STM32G474RBTx Bootloader + G1_Hand Firmware

## Build

- **Toolchain**: `arm-none-eabi-gcc` (default preset). `starm-clang` is also configured for STM32CubeIDE.
- **Generator**: Ninja

```bash
cmake --preset Debug
cmake --build build/Debug
```

A `Release` preset (`-Os -g0`) also exists. Post-build, `.hex` and `.bin` are generated via `arm-none-eabi-objcopy`.
- Convenience scripts: `build.bat` (Windows) / `build.sh` (Linux/macOS) — auto-locate toolchain, configure, and build.
- `.clangd` points `CompilationDatabase` to `build/Release` — use the Release preset for IDE intellisense.
- STM32CubeIDE wraps cmake via `cube-cmake` (`.vscode/settings.json`). Outside the IDE, use system cmake directly.

## Architecture

**No RTOS.** All periodic work runs through `sw_timer` — a cooperative scheduler driven by `sw_timer_task()` in the main loop. SysTick ISR calls `sw_timer_tick()`; the main loop dispatches expired timer callbacks by priority (HIGH → NORMAL → LOW).

### Four-layer architecture

```
tasks/          → Glue: owns sw_timers, wires drivers to services
service/        → Domain logic (boot FSM, LED FSM). No HAL calls.
device_drivers/ → HAL wrappers (CAN, UART, Flash, LED, systick)
m_middlewares/  → Reusable frameworks and algorithms
```

Tasks are thin. Services receive callback function pointers for hardware access instead of calling HAL directly. Device drivers expose `init()`/`deinit()`/`is_initialized()` lifecycles.

### Bootloader

This is a **dual A/B partition** CAN/CAN FD firmware upgrade system. Protocol details in `boot_design.md`. Key points:
- CAN IDs `0x701` (Host→Node) and `0x702` (Node→Host)
- Frame payload size negotiated at START (Classic CAN: 8 bytes, CAN FD: up to 64 bytes)
- 2-byte header (Command + Sequence), checksum at fixed Byte 2-3 to avoid CAN FD padding issues
- Boot service lives in `service/boot/` (FSM `boot_fsm`, flash ops `boot_flash`, transport `boot_transport`)

## Project structure

| Directory | Ownership | Notes |
|---|---|---|
| `Core/` | CubeMX **generated** | HAL init, peripheral config, `main.c`. `USER CODE BEGIN`/`END` guards. |
| `Drivers/` | **Vendor** (ST) | HAL + CMSIS. Do not modify. |
| `cmake/stm32cubemx/` | CubeMX **generated** | CMake config for HAL sources. |
| `tasks/` | **User** | App glue: `app_main.c`, `boot_task`, `led_task`, `log_task`. |
| `device_drivers/` | **User** | HAL wrappers: CAN, UART (DMA + IDLE-line RX), Flash, LED, systick. |
| `service/` | **User** | Domain logic: `boot/` (bootloader FSM), `led` (LED state machine). |
| `m_middlewares/` | **User** | Frameworks (`sw_timer`, `fsm`, `event`, `daemon`, `msg_fifo`, `kfifo`, `clist`, `log`, `protocol_packer`/`parser`, `key_base`) + algorithms (PID, PLL, CRC, filters, math). |
| `m_middlewares/public.h` | **User** | Central include umbrella under `extern "C"`. Most middleware accessible via this single include. |
| `stm32_g474_boot.ioc` | CubeMX **config** | Source of truth for pin mux, clocks, peripheral assignment. |

## Entry point

```
Reset_Handler (startup_stm32g474xx.s)
  → SystemInit()
  → main()                          [Core/Src/main.c]
       → HAL_Init() → SystemClock_Config() (160 MHz)
       → MX_*_Init()                (GPIO, DMA, UART, SPI, FDCAN1/2, TIM1, TIM15)
       → app_main()                 [tasks/app_main.c]
            → boot_task_try_boot_app()  // Jump to valid App if present
            → boot_task_init()          // Otherwise start bootloader
            → while(1) { sw_timer_task() }
```

`main.c`'s own `while(1)` after the `app_main()` call is **unreachable dead code** — `app_main()` contains its own infinite loop. Only add new peripheral init calls inside `USER CODE BEGIN 2` in `main.c`; the loop body under `USER CODE BEGIN WHILE` is unused.

## Coding conventions

Follow **`MODULE_CODING_GUIDE.md`** for all new code. Key rules:

- **Style**: WebKit (4-space indent, Allman braces for functions, K&R braces for control flow). Max 100 chars/line.
- **Standard**: C11 (`-std=gnu11`), hard float (`-mfloat-abi=hard`). MISRA C:2012 expected.
- **Naming**: `snake_case` with module prefix (`module_name_`). Types end in `_t`. Error enums start with `MODULE_OK = 0`. Static globals: `s_` prefix.
- **config-in-context**: Every module has `xxx_config_t` (immutable params + callbacks) inside `xxx_context_t` (runtime state + config copy + `initialized` flag). Init copies config, sets `initialized = true`. Public functions guard with `if (!ctx->initialized)`.
- **Static allocation**: No `malloc` in middleware/services. Callers provide memory via `xxx_register_static()`.
- **Types**: Fixed-width (`uint8_t`, `uint16_t`, `uint32_t`) and `bool` from `<stdbool.h>`.
- **Comments**: Chinese Doxygen (`@brief`/`@param`/`@return`) for public API. CubeMX files may use GBK; user files use UTF-8.
- **Section order**: `/* Includes */` → `/* Private constants */` → `/* Private variables */` → `/* Private function prototypes */` → `/* Exported functions */` → `/* Private functions */`.
- **Dynamic memory**: If `__malloc` is used, must immediately `memset(ptr, 0, size)` to zero-initialize (uninitialized `initialized` flag causes nullptr dereference in deinit).
- No tests, no CI, no formatter/linter configured.

## CubeMX code regeneration

Re-generating from `stm32_g474_boot.ioc` **overwrites**:
- `Core/Inc/`, `Core/Src/`
- `cmake/stm32cubemx/CMakeLists.txt`

User code inside `/* USER CODE BEGIN ... */` / `/* USER CODE END ... */` guards is preserved. Everything outside is lost. Only add code between guards in CubeMX-managed files. User directories (`tasks/`, `device_drivers/`, `m_middlewares/`, `service/`) are unaffected.

## Hardware

- **MCU**: STM32G474RBTx — Cortex-M4 + FPU, 128KB Flash, 128KB RAM
- **Clock**: 160 MHz from HSE + PLL (Voltage Scale 1 + Boost, Flash latency 4)
- **Linker**: `STM32G474XX_FLASH.ld` — heap 512B, stack 1024B, newlib-nano, `--gc-sections`
- **Toolchain flags**: `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`
- **Peripherals**: SPI1, FDCAN1 (PA11/PA12), FDCAN2 (PB12/PB13), USART1 (DMA TX + IDLE-line RX), TIM1, TIM15, GPIO, DMA
