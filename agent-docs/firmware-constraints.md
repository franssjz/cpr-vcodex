# Firmware Constraints

Read this before touching firmware code, render paths, EPUB parsing, storage, or
input handling.

## Hardware Budget

- Target: Xteink X4, ESP32-C3, single-core RISC-V at 160 MHz.
- RAM: about 380 KB usable, no PSRAM.
- Flash: 16 MB.
- Display: 800x480 monochrome e-ink.
- Framebuffer: 48,000 bytes. The project uses single-buffer mode.
- Storage: SD card plus `.crosspoint/` caches.

## Memory Rules

- Keep large buffers off the stack. Treat local arrays above 256 bytes as risky.
- Allocate large buffers once per activity when possible, then release in
  `onExit()`.
- Always check `malloc`/`new` results and log before returning failure.
- Avoid repeated `new`/`delete`, `std::vector` growth, and temporary
  `std::string` construction in hot paths.
- If a vector is necessary, call `.reserve()` before push loops.
- Prefer `static constexpr` for constants and lookup tables.
- Large static data should remain in flash, not DRAM.

## C++ And Platform Pitfalls

- Build uses C++20-ish flags through PlatformIO, with exceptions disabled.
- Do not use exceptions or RTTI-based designs.
- `std::string_view` is not null-terminated. Do not pass `.data()` to C APIs
  unless you first copy to a null-terminated buffer.
- ESP32-C3 can fault on unaligned multi-byte loads. Use `memcpy` from raw byte
  buffers instead of pointer casts.
- `IRAM_ATTR` is required for ISR handlers. Data used by ISR code must be safe
  while flash cache is suspended.
- Do not call task mutex APIs from ISR context. Use the `FromISR` FreeRTOS APIs.

## Rendering And UI

- Never hardcode 800 or 480 for layout. Use renderer screen dimensions and
  oriented viewable bounds.
- Use `MappedInputManager::Button` logical buttons in activities. Raw hardware
  button IDs belong only in mapping code.
- Use UITheme/GUI patterns for layout instead of direct one-off positioning
  unless the surrounding code already does otherwise.
- User-facing strings must go through the translation flow with `tr()`.

## Files To Inspect

- `platformio.ini`: build flags and environments.
- `partitions.csv`: app partition sizes and OTA layout.
- `src/main.cpp`: activity lifetime and global setup.
- `src/MappedInputManager.cpp`: logical button mapping.
- `lib/hal/`: storage, display, and GPIO wrappers.
- `lib/GfxRenderer/GfxRenderer.cpp`: framebuffer and rendering buffers.
- `src/fontIds.h`: font IDs used by the renderer.
