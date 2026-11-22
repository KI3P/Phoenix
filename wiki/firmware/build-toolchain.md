---
title: Build Toolchain (Teensyduino 1.62) and Optimize-Setting Results
type: decision
status: stable
created: 2026-11-11
updated: 2026-11-11
tags: [build, toolchain, teensyduino, gcc, optimiser, cpu-speed, arduino-cli, teensy]
source_refs: []
related: ["[[og-startup-hang]]", "[[memory-layout-and-stack]]", "[[spi-display-hang]]", "[[hardware-platform]]", "[[documentation-todos]]"]
---

# Build Toolchain (Teensyduino 1.62) and Optimize-Setting Results

On 2026-11-11 the development machine was upgraded from **Teensyduino 1.59** (GCC 11.3.1) to
**Teensyduino 1.62** (GCC 15.2.1). The main reason was the Debug (`-Og`) startup hang in
[[og-startup-hang]], which PJRC fixed in 1.60. This page records how the upgrade was checked,
and which Optimize / CPU speed settings work on 1.62.

## What changed

- **Teensyduino** went from 1.59 to 1.62. `arduino-cli` and the Arduino IDE share
  `~/.arduino15`, which keeps one version per platform.
  - Upgrade: `arduino-cli core install teensy:avr@1.62.0`.
  - Rollback: `arduino-cli core install teensy:avr@1.59.0`.
- **Compiler:** GCC 11.3.1 → 15.2.1. Phoenix and its libraries compile with no errors.
- **Core startup:** `reset_PFD()` is gone, so Debug builds start ([[og-startup-hang]]).
- **SPI library:** unchanged in the parts that matter. `SPI.begin()` still sets the 240 MHz
  LPSPI root, and `transfer()` still waits with no timeout. Phoenix's 132 MHz fix
  ([[spi-display-hang]]) is still needed and applies unchanged.
- **New Optimize options:** "Faster + Nano Libc" (`o2stdnano`) and "Faster with LTO + Nano
  Libc" (`o2ltonano`). Both link newlib-nano, which gives more stack than plain `o2std`.
- **New GCC 15 warnings in Phoenix:**
  - `RFBoard_si5351.cpp` (`set_pll`, `set_ms`, `set_vcxo`) allocated its 20-byte register
    array with `new[]` and freed it with plain `delete`, which is undefined behaviour. It is
    now a local array, which also removes a heap allocation from every PLL change.
  - `UpdateZoomSetting()` (`MainBoard_DisplayHome.cpp`) used a 4-byte `sprintf` buffer, now
    5 bytes.

## Results on 1.62

Each build was flashed and polled with the read-only CAT `FA;` every 2 s for 45 s
(`usb=serialmidiaudio`). Stack is "free for local variables" from the build output (see
[[memory-layout-and-stack]]).

| Optimize setting | Stack free | 528 MHz | 600 MHz |
|---|---|---|---|
| Smallest Code with LTO (`oslto`) | 120 KB | pass | pass |
| Smallest Code (`osstd`) | 88 KB | pass | pass |
| Faster + Nano Libc (`o2stdnano`) | 53 KB | pass | pass |
| Fast with LTO (`o1lto`) | 50 KB | pass; 10 min 300/300 | pass; 10 min 300/300 |
| Faster with LTO + Nano Libc (`o2ltonano`) | 20 KB | pass | pass |
| Fast (`o1std`), Faster (`o2std`) | 17.8 KB | pass | pass |
| Debug (`ogstd`) | 17.7 KB | pass | pass |
| Faster with LTO (`o2lto`) | 17.5 KB | pass | pass |
| Fastest, ± LTO (`o3std`/`o3lto`) | −48 KB | does not fit in RAM1 | does not fit |

- The 528 MHz column was built before the GCC 15 warning fixes. The 600 MHz column includes
  them.
- Peak stack use is about 3.5 KB since the [[memory-layout-and-stack]] fix, so 17.5 KB is now
  comfortable headroom.
- Unit tests are unchanged, since they use the host compiler.

## How the upgrade was checked: an isolated trial

To try a new Teensyduino without touching the installed one, give `arduino-cli` its own data
directory:

```yaml
# cli.yaml
board_manager:
  additional_urls: [https://www.pjrc.com/teensy/package_teensy_index.json]
directories:
  data: <trial>/data
  downloads: <trial>/downloads
  user: <trial>/user
```

1. Install the new version into the trial directory:
   `arduino-cli --config-file cli.yaml core update-index`, then
   `arduino-cli --config-file cli.yaml core install teensy:avr@<ver>`.
2. Build with `arduino-cli --config-file cli.yaml compile ... --libraries ~/Arduino/libraries`.
3. Upload the hex with the **installed** tools: `arduino-cli upload --fqbn teensy:avr:... --input-file <absolute path to hex>`.
   An Optimize option the installed version doesn't know (for example `o2stdnano` on 1.59)
   must be swapped for a known one in the upload FQBN. The option doesn't affect uploading.

Close the Arduino IDE (its `arduino-cli daemon` uses `~/.arduino15`) and the Teensy Loader
window before installing over the real platform.

## Open

- `code/.vscode/arduino.json` (600 MHz, Fast with LTO) is now tested on 1.62. Faster settings
  are an option if you want them.
- 720 MHz and above were not re-tested on 1.62.
