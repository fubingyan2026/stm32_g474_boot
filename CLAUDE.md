# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

- **Toolchain**: `arm-none-eabi-gcc` (default preset). STM32CubeIDE wraps cmake via `cube-cmake` and also supports `starm-clang` (see `cmake/starm-clang.cmake`).
- **Generator**: Ninja

```bash
cmake --preset Debug
cmake --build build/Debug
```

Four presets are available: `Debug`, `Release` (`-Os -g0`), `RelWithDebInfo`, `MinSizeRel`. Post-build, `.hex` and `.bin` files are generated via `arm-none-eabi-objcopy`. `compile_commands.json` is exported to the build directory for clangd.

### Convenience scripts

- **Windows**: `build.bat [-t Debug|Release]` — auto-locates ARM toolchain (STM32CubeIDE bundle or PATH), cmake, and ninja; configures and builds.
- **Linux/macOS**: `build.sh [-t Debug|Release]` — same logic, searches common toolchain paths.

### clangd

`.clangd` points `CompilationDatabase` to `build/MinSizeRel`. For IDE intellisense, use the MinSizeRel preset so `compile_commands.json` is available at that path. (To switch, edit `.clangd` and reconfigure with the matching preset.)

## Flash layout

The STM32G474 has 128 KB of internal flash. The linker script (`STM32G474XX_FLASH.ld`) only maps the **bootloader's own 36 KB** region — App partitions are accessed at runtime via absolute-address flash operations, not linker symbols.

| Region | Address | Size | Notes |
|--------|---------|------|-------|
| Bootloader | `0x08000000` | 36 KB | Linked region; contains this project |
| App A | `0x08009000` | 36 KB | Active/standby firmware slot |
| App B | `0x08012000` | 36 KB | Alternate firmware slot |
| Metadata | `0x0801B000` | 4 KB | Aligned to min page size (4 KB); stores `boot_metadata_t` |

> **Note:** Sizes and addresses are computed from macros in [`boot_flash.c`](service/boot/boot_flash.c#L16-L19): `BOOT_FLASH_BOOT_SIZE = 0x9000`, `BOOT_FLASH_APP_SIZE = 0x9000`, `BOOT_FLASH_META_SIZE = 0x1000`. The protocol spec ([`boot_protocol_spec.md`](boot_protocol_spec.md)) documents App partitions as 40 KB — if that is the intended target, the code macros need updating.

The flash driver (`drv_stm32g4_flash`) auto-detects single-bank (4 KB pages) vs. dual-bank (2 KB pages) mode at init by reading the `FLASH->OPTR` DBANK bit, and handles cross-bank erase by splitting operations at bank boundaries.

## Project structure

| Directory | Ownership | Notes |
|---|---|---|
| `Core/` | CubeMX **generated** | HAL init, `main.c`, peripheral config (`gpio.c`, `fdcan.c`, `spi.c`, etc.). Contains `USER CODE BEGIN`/`END` guards. |
| `Drivers/` | **Vendor** (ST) | HAL + CMSIS. Do not modify. |
| `cmake/stm32cubemx/` | CubeMX **generated** | CMake config for HAL sources. Regenerated with CubeMX. |
| `tasks/` | **User** | Application glue layer. `app_main` lives here; each task owns `sw_timer` instances and wires drivers to services. |
| `device_drivers/` | **User** | Hardware abstraction layer (CAN, UART, Flash, LED, systick). Direct HAL usage lives here. |
| `service/` | **User** | Domain logic layer (`boot/` FSM + flash + transport, `led` FSM). No HAL calls — receives callbacks for hardware access. |
| `m_middlewares/` | **User** | Reusable frameworks (`sw_timer`, `fsm`, `event`, `daemon`, `msg_fifo`, `kfifo`, `clist`, `log`, `protocol_packer`/`parser`, `key_base`) and algorithms (PID, gimbal PID, MIT control, filters, math, PLL, CRC8/CRC16/CRC32). |
| `m_middlewares/public.h` | **User** | Central include umbrella under `extern "C"` — application code includes only this to get all middleware headers. |
| `updata_tool/` | **User** | Host-side Python/PySide6 flashing tool that drives the bootloader over CAN (via CANable USB-CAN adapter). |
| `stm32_g474_boot.ioc` | CubeMX **config** | Source of truth for pin mux, clocks, and peripheral assignment. |
| `.kilo/` | **AI-generated** | Planning directory managed by Claude Code. Do not commit or modify manually. |

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
            → boot_task_try_boot_app()   // Validate App checksum, jump if valid
            → boot_task_init()           // Enter bootloader mode if no valid App
            → while(1) { sw_timer_tick(); sw_timer_task() }
```

`main.c`'s own `while(1)` after the `app_main()` call is **unreachable dead code** — `app_main()` contains its own infinite loop. Only add new peripheral init calls inside `USER CODE BEGIN 2` in `main.c`; the loop body under `USER CODE BEGIN WHILE` is unused.

There is **no RTOS**. All periodic work runs through `sw_timer` — a cooperative scheduler driven by `sw_timer_task()` in the main loop. SysTick ISR calls `sw_timer_tick()` to mark expired timers ready; the main loop dispatches their callbacks by priority (HIGH → NORMAL → LOW). Adding periodic work always means creating a new `sw_timer`.

### Four-layer architecture

```
tasks/          → Glue: owns sw_timers, wires drivers to services
service/        → Domain logic (boot FSM, flash ops, transport; LED FSM)
device_drivers/ → HAL calls, DMA, interrupts, kfifo buffering
m_middlewares/  → Reusable frameworks and algorithms
```

Tasks are thin — they own timers and stitch layers together. Services contain business logic but never touch HAL directly (they receive callback function pointers instead). Device drivers wrap HAL and expose `init()`/`deinit()`/`is_initialized()` lifecycles.

### Bootloader design

This project implements a **dual A/B partition** firmware upgrade system over CAN/CAN FD. The protocol is fully specified in [`boot_protocol_spec.md`](boot_protocol_spec.md). A critical bug fix for CAN FD DLC padding is documented in [`can_fd_dlc_padding_fix.md`](can_fd_dlc_padding_fix.md).

**Protocol basics:**
- CAN IDs `0x701` (Host→Node) and `0x702` (Node→Host) — Host ID configurable in the GUI, Node ID = Host ID + 1
- 2-byte header (Command + Sequence) per frame
- 1 KB block checksums (16-bit additive) with checksum at fixed offset (Byte 2-3) to avoid CAN FD padding issues
- Full-image verification: 32-bit additive checksum (`sum(fw_data) & 0xFFFFFFFF`) instead of CRC32
- Commands: `START` (0x01), `METADATA` (0x02), `DATA` (0x03), `VERIFY` (0x04), `REBOOT` (0x05), `CANCEL` (0x06), `DATA_START` (0x07), `DATA_END` (0x08), `ACK` (0x10), `NACK` (0x11)
- Support for both Classic CAN (8-byte frames) and CAN FD (up to 64-byte frames), frame size negotiated at START from the discrete set `{8, 12, 16, 20, 24, 32, 48, 64}`

**Boot service layer (`service/boot/`):**

| Component | File | Role |
|---|---|---|
| Transport | `boot_transport.c` | Stateless frame encode/decode. Parses all 10 command types and builds ACK/NACK responses. |
| FSM | `boot_fsm.c` | Upgrade state machine (5 states: IDLE → START → DATA_TRANSFER → VERIFY_PENDING → REBOOT_PENDING). Uses the `fsm` library's guard matrix for transition validation. Drives the upgrade through callbacks — never touches hardware directly. |
| Flash | `boot_flash.c` | Partition-aware flash manager. Erases/writes/verifies App partitions (size defined by `BOOT_FLASH_APP_SIZE`), manages the metadata page, computes checksums over flash. Delegates to `drv_stm32g4_flash` (`ef_port_*` API). |

**Upgrade flow (4 phases):**
1. **Handshake**: START (negotiate frame size, validate HW compat ID, erase target partition) → METADATA (32-bit additive checksum + version)
2. **Data transfer**: Per 1 KB block: DATA_START (block index handshake) → N × DATA frames → DATA_END (16-bit additive checksum). Block-level retry (up to 3×) on checksum failure. Double-verified: checksum on wire + byte-by-byte read-back after flash write. CANCEL (0x06) accepted at any time to abort and return to IDLE.
3. **Verify**: Full-image 32-bit additive checksum computed over the written flash partition, compared with host-provided value.
4. **Commit & reboot**: Write metadata page (magic `0x424F4F54`, partition, version, checksum), then NVIC system reset.

**CAN FD DLC padding caveat:** CAN FD data lengths are discrete (`{8, 12, 16, 20, 24, 32, 48, 64}`). When a DATA_END frame doesn't exactly fill a discrete length, the host pads with zero bytes. The board-side parser must **cap `rem_len` by free buffer space** rather than trusting the DLC-derived length — see [`can_fd_dlc_padding_fix.md`](can_fd_dlc_padding_fix.md) for the full analysis.

**Boot decision (`boot_task_try_boot_app`):**
1. Read metadata page at `0x0801B000` (computed as `BOOT_FLASH_APP_B_ADDR + BOOT_FLASH_APP_SIZE`)
2. If `magic != 0x424F4F54` or `upgrade_flag != 0` or App 32-bit additive checksum mismatch → enter bootloader
3. Otherwise → jump to App (currently commented out; returns `true`)

**A/B swap logic:** New firmware always goes to the *opposite* partition of the currently-active App. The old partition is never erased until the new firmware is fully verified and committed, ensuring an unbrickable update.

### Middleware ecosystem

- **`sw_timer`** — Software timers with priority levels. Single-shot, N-repeat, or infinite. Backbone of all periodic work.
- **`fsm`** — Flat state machine with guard-matrix transition validation. Used by `service/led` and `service/boot/boot_fsm`. Not included in `public.h` (include manually).
- **`event`** — ISR-safe 32-bit event flags for main-loop polling.
- **`daemon`** — Task watchdog with online/offline callbacks and debounce.
- **`kfifo`** — Lock-free power-of-2 ring buffer. Single-producer/single-consumer safe from ISR. Used for all DMA buffering.
- **`clist`** — Intrusive circular doubly-linked list (Linux-kernel style). Backbone of `daemon`, `sw_timer`, `key_base` instance registries.
- **`msg_fifo`** — Typed message queue layered on `kfifo`. Used to decouple CAN ISR from main-loop FSM processing (256-slot queue).
- **`log`** — ESP32-style colored logging (`LOG_E`/`W`/`I`/`D`/`T` macros). Output buffered through a `kfifo` for non-blocking DMA TX.
- **`protocol_packer`/`protocol_parser`** — Stateless frame builder / stateful frame de-serializer with configurable header/footer/checksum callbacks. Generic — not bootloader-specific.
- **`key_base`** — Button debounce with multi-event detection (click, double-click, long press, etc.).
- **Algorithms**: PID (position/incremental), gimbal PID (cascade + gyro feedforward), MIT impedance control, PT1/Biquad/Slew/LMA filters, fast trig (`sin_approx`, `atan2_approx`), median filters, software PLL, CRC8/CRC16/CRC32.

## Host-side flashing tool (`updata_tool/`)

A Python/PySide6 desktop application that drives the bootloader over CAN via a CANable 2.5 USB-CAN adapter (Candlelight/ElmueSoft firmware).

```
updata_tool/
├── flash_gui.py              ← Convenience launcher at project root
├── canable_sdk/              ← USB-CAN driver (ZDTCanable, CANFrame, bitrate config)
├── flash_tool/               ← The flashing application
│   ├── __main__.py           ← Entry: python -m updata_tool.flash_tool
│   ├── main_window.py        ← PySide6 QMainWindow (device panel + firmware panel)
│   ├── protocol.py           ← Protocol codec (frame builders, parsers, checksum/CRC)
│   ├── worker.py             ← FlashWorker QThread — the 4-phase flashing state machine
│   └── widgets/              ← DevicePanel + FirmwarePanel
└── cangui/                   ← Separate CAN bus monitor GUI (not part of flashing)
```

To run: `python flash_gui.py` or `python -m updata_tool.flash_tool`. Requires PySide6, pyusb, and the bundled `libusb-1.0.dll` (Windows). On Linux, run `install_udev.sh` for USB permissions.

The `FlashWorker` state machine mirrors the board-side FSM: handshake → data transfer (with 3-retry block-level error recovery) → verify → reboot.

## Coding conventions

Follow **`MODULE_CODING_GUIDE.md`** for all new code. Key rules:

- **Style**: WebKit (4-space indent, Allman braces for functions, K&R braces for `if`/`for`/`while`). Max 100 chars per line.
- **Standard**: C11 (`-std=gnu11`), MISRA C:2012 compliance expected.
- **Naming**: `snake_case` with module prefix. Types end in `_t` (e.g. `protocol_parser_config_t`). Error enums start with `MODULE_OK = 0`. Static globals use `s_` prefix.
- **config-in-context pattern**: Every module has a `xxx_config_t` (immutable parameters + callbacks) nested inside a `xxx_context_t` (runtime state + copy of config + `initialized` flag). Init copies config, sets `initialized = true`. All public functions guard with `if (!ctx->initialized) return ERROR_UNINITIALIZED`.
- **Static allocation**: No `malloc` in middleware or services. Callers provide memory; modules use `xxx_register_static()` patterns.
- **`__malloc` zero-init**: If `__malloc` is used, it **must** be immediately followed by `memset(ptr, 0, size)`. Uninitialized memory can contain garbage values for `initialized` flags and pointers, causing crashes when `_deinit()` dereferences a garbage pointer. See `MODULE_CODING_GUIDE.md` § "动态内存分配必须初始化" for root-cause analysis.
- **Comments**: Chinese Doxygen-style (`@brief`/`@param`/`@return`) for public API. Source comments are mixed Chinese (GBK in CubeMX files, UTF-8 in user files).
- **Types**: Always use fixed-width integers (`uint8_t`, `uint16_t`, `uint32_t`) and `bool` from `<stdbool.h>`.
- **Section ordering**: `/* Includes */` → `/* Private constants */` → `/* Private variables */` → `/* Private function prototypes */` → `/* Exported functions */` → `/* Private functions */`.

## CubeMX code regeneration

Re-generating from the `.ioc` file **overwrites** `Core/` and `cmake/stm32cubemx/`. Code inside `/* USER CODE BEGIN ... */` / `/* USER CODE END ... */` guards in `Core/Src/main.c` is preserved; everything outside is lost. Only add code between the guards in CubeMX-managed files. User directories (`tasks/`, `device_drivers/`, `m_middlewares/`, `service/`) are unaffected.

## Hardware

- **MCU**: STM32G474RBTx — Cortex-M4 with FPU, 128 KB Flash (`0x08000000`), 128 KB RAM (`0x20000000`)
- **Clock**: 160 MHz from HSE + PLL (Voltage Scale 1 + Boost, Flash latency 4)
- **Linker**: `STM32G474XX_FLASH.ld` — heap 512 B, stack 1024 B, newlib-nano, `--gc-sections`. Maps bootloader's 36 KB flash region.
- **Toolchain flags**: `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`
- **Peripherals**: SPI1, FDCAN1 (PA11/PA12), FDCAN2 (PB12/PB13), USART1 (DMA TX + IDLE-line RX), TIM1, TIM15, GPIO, DMA
- No tests, no CI, no formatter/linter configured.
