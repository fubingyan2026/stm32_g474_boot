# AGENTS.md — STM32G474 Bootloader

> **Canonical reference**: [CLAUDE.md](CLAUDE.md) — read that first. This file is a quick-index; CLAUDE.md is the authoritative source.

## Quick start

```bash
cmake --preset Debug && cmake --build build/Debug   # or: build.bat / build.sh
```

## Architecture at a glance

```
No RTOS — cooperative sw_timer scheduler
tasks/ → service/ → device_drivers/ → m_middlewares/
```

[`boot_protocol_spec.md`](boot_protocol_spec.md) defines the CAN/CAN FD dual A/B upgrade protocol (32-bit checksum verification). [`can_fd_dlc_padding_fix.md`](can_fd_dlc_padding_fix.md) documents a subtle CAN FD data-length issue.

## Coding rules

Follow [`MODULE_CODING_GUIDE.md`](MODULE_CODING_GUIDE.md). Key points:
- C11, WebKit style, 4-space indent, fixed-width types, `snake_case` with module prefix
- `config-in-context` pattern: `xxx_config_t` inside `xxx_context_t`, `initialized` guard on all public functions
- Static allocation; if `__malloc` is used → immediately `memset(ptr, 0, size)`
- CubeMX files: only add code between `USER CODE BEGIN/END` guards

## Key files

| File | Purpose |
|------|---------|
| `tasks/boot_task.c` | Glue: ISR → FIFO → FSM → flash callbacks |
| `service/boot/boot_fsm.c` | 5-state upgrade state machine with guard matrix |
| `service/boot/boot_flash.c` | A/B partition flash manager |
| `service/boot/boot_transport.c` | Stateless CAN protocol frame codec |
| `device_drivers/drv_stm32g4_flash.c` | Low-level flash HAL (EasyFlash `ef_port_*` API) |
| `device_drivers/drv_can.c` | FDCAN driver with interrupt-driven RX |
| `updata_tool/flash_tool/worker.py` | Host-side flashing state machine (Python/PySide6) |
| `STM32G474XX_FLASH.ld` | Linker script — only maps 32 KB bootloader region |
