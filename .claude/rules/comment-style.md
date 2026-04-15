# Comment Style

Applies to all code in `examples/` and `libs/cli/`. Does **not** apply to `libs/freertos/` (third-party kernel).

## Function Documentation Block

Every function (including `static` helpers) must have a block comment immediately above its definition:

```c
/**
 * @brief <One-sentence summary of what the function does.>
 *
 * <Longer description if needed. Explain the algorithm, state machine step,
 * or protocol detail. Keep it factual — no padding.>
 *
 * @why <One sentence: why this function is needed in this example/context.>
 *
 * @param <name>  <Description.>   (omit if no parameters)
 * @return        <Description.>   (omit if void)
 */
```

### Rules

- All three sections (`@brief`, `@why`, params/return) are required for non-trivial functions.
- Trivial one-liners (e.g., `Error_Handler` infinite loop, MSP init callbacks that only enable a clock) need only `@brief` and `@why` — no `@param`/`@return`.
- HAL MSP callbacks (`HAL_MspInit`, `HAL_UART_MspInit`, etc.) must include a `@why` explaining which peripheral they configure and why it is used in this example.
- **Do not add comments to HAL API calls** (functions starting with `HAL_`, `MX_`, or any STM32 HAL library function) — they are provided by STMicroelectronics.
- Do **not** restate the function name or parameter names verbatim — add meaning, not noise.
- Use `/** ... */` style (Doxygen-compatible), not `//` line comments for function headers.

### Example

```c
/**
 * @brief Calculates CRC32 using the standard Ethernet (IEEE 802.3) polynomial.
 *
 * Processes data byte-by-byte with table-free bit-by-bit computation.
 * The initial value is 0xFFFFFFFF and the result is bitwise-inverted before
 * returning, matching the standard CRC32 convention.
 *
 * @why Required to verify firmware integrity after flash write and to validate
 *      OTA packet checksums before accepting any chunk.
 *
 * @param data  Pointer to the byte buffer to checksum.
 * @param len   Number of bytes to process.
 * @return      32-bit CRC value.
 */
static uint32_t CRC32_Calculate(const uint8_t *data, uint32_t len)
```

## Inline Comments

Add `//` inline comments for code that is not immediately obvious.

### When to add inline comments

- Complex algorithms — multi-step calculations, bit manipulations, non-obvious logic
- State machine transitions
- Protocol steps — multi-phase sequences (e.g., START → DATA → END)
- Safety checks — bounds checking, overflow prevention, error recovery
- Critical timing — operations with specific timing requirements
- Memory operations — flash writes, buffer management, address calculations
- Interrupt handling — ISR setup, context switching, shared resource access

### When NOT to add inline comments

- Obvious operations — variable assignments, simple loops, self-explanatory code
- HAL API calls — standard ST HAL functions (`HAL_UART_Transmit`, `HAL_FLASH_Program`, etc.)
- Trivial arithmetic or comparisons
- Standard C idioms (`if (ptr != NULL)`, `for (i = 0; i < len; i++)`)

### Inline comment style

- Use `//` on the same line or immediately above the code block
- Keep comments concise — 1–2 lines maximum
- Focus on the "why" and "what" of complex logic
- Use consistent capitalisation and punctuation

### Example

```c
// Extract firmware information from START packet
uint32_t total_chunks = pkt.total_chunks;
uint32_t total_fw_size = pkt.total_fw_size;

// Validate firmware size to prevent buffer overflows
if (total_fw_size == 0 || total_fw_size > APP_B_MAX_SIZE) {
    OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, 0, ERR_FLASH_FULL);
    return;
}
```
