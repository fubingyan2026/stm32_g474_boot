# CRC32 Mismatch — Root Cause Diagnosis Plan v5

## Status

- ✅ Byte dump at offset 0 correct: `00 00 02 20  31 3D 00 08`
- ❌ **All three sub-regions wrong** vs firmware file (31048 bytes):

| Check | Device | Expected | Match? |
|-------|--------|----------|--------|
| 25p (off 7762) | 0x3E554404 | 0x29649EDC | ❌ |
| 50p (off 15524) | 0x60E000C7 | 0xD2A03192 | ❌ |
| 75p (off 22262) | 0xBC7D7207 | 0x6B3C6B01 | ❌ |

- ❌ Full CRC32 wrong: expected `0x9DB6E136`, computed `0xF31AF90B`

## Key finding

The byte dump at offset 0 is correct, but by offset 7762 (25%) the data is already wrong. The corruption begins somewhere in the first ~7.5KB (between byte 0 and byte 7762). This is approximately blocks 0-7 (each 1024 bytes).

## Plan: Find exact corruption onset with block-level scanning

Replace the 25p/50p/75p diagnostics with CRC32 checks at block boundaries 1KB, 2KB, 3KB:

### Code change in `boot_flash_compute_crc32`:

```c
/* Block-level scan to find first corrupted block */
if (size >= 4096U) {
    uint32_t b1 = get_CRC32_check_sum((const uint8_t*)(addr + 1024U), 1024U, 0xFFFFFFFFU);
    uint32_t b2 = get_CRC32_check_sum((const uint8_t*)(addr + 2048U), 1024U, 0xFFFFFFFFU);
    uint32_t b3 = get_CRC32_check_sum((const uint8_t*)(addr + 3072U), 1024U, 0xFFFFFFFFU);
    uint32_t b4 = get_CRC32_check_sum((const uint8_t*)(addr + 4096U), 1024U, 0xFFFFFFFFU);
    LOG_I("flash", "CRC32 b1=0x%08lX b2=0x%08lX b3=0x%08lX b4=0x%08lX",
        (unsigned long)b1, (unsigned long)b2,
        (unsigned long)b3, (unsigned long)b4);
}
```

### Expected cross-check

```python
import zlib; data = open("build/Release/stm32_g474_boot.bin","rb").read()
def crc(r): return zlib.crc32(r) & 0xFFFFFFFF
print(f"b1=0x{crc(data[1024:2048]):08X}")
print(f"b2=0x{crc(data[2048:3072]):08X}")
print(f"b3=0x{crc(data[3072:4096]):08X}")
print(f"b4=0x{crc(data[4096:5120]):08X}")
```

### Decision tree

| Blocks that MATCH | Meaning |
|---|---|
| b1, b2, b3, b4 all match | Corruption starts at block 4+ (offset >4096); extend scan to b5-b8 |
| b1, b2, b3 match, b4 differs | Corruption starts at block 4 (offset 4096) |
| b1, b2 match, b3 differs | Corruption starts at block 3 (offset 3072) |
| b1 matches, b2 differs | Corruption starts at block 2 (offset 2048) |
| b1 differs | Corruption starts at block 1 (offset 1024) |

Once the first corrupted block is identified, we know the exact boundary where the write path starts producing incorrect data.

## Files to modify

| File | Action |
|------|--------|
| `service/boot/boot_flash.c` | Replace 25p/50p/75p block with b1-b4 block-level scan |

## Validation

1. Build, run upgrade, capture `CRC32 b1=... b2=... b3=... b4=...` line
2. Cross-check all four against firmware file
3. First mismatched block number tells us exactly where corruption begins
