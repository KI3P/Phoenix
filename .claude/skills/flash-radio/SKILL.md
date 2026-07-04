---
name: flash-radio
description: Compile the PhoenixSketch production firmware and flash it to the Teensy 4.1 radio over USB. Use when the user asks to build, compile, upload, flash, deploy, or program the radio / Teensy / Phoenix firmware.
---

# flash-radio

Compiles `code/src/PhoenixSketch/PhoenixSketch.ino` with `arduino-cli` and uploads the resulting `.hex` to the connected Teensy 4.1.

The build settings come from `code/.vscode/arduino.json` and must stay in sync with it:

| Setting | Value |
| --- | --- |
| Board (FQBN base) | `teensy:avr:teensy41` |
| FQBN options | `usb=serial2,speed=600,opt=o1lto,keys=en-us` |
| Sketch | `src/PhoenixSketch/PhoenixSketch.ino` (relative to `code/`) |
| Output dir | `../ArduinoOutput` (relative to `code/`) |
| Default port | `/dev/ttyACM0` |

If those values diverge from `code/.vscode/arduino.json`, prefer the file and tell the user.

## Steps

1. **Check the board is connected.** Run `arduino-cli board list` and confirm a Teensy is present (it usually appears as `/dev/ttyACM0`, FQBN `teensy:avr:teensy41`). If no Teensy is listed, stop and tell the user to connect/power the radio — do not try to flash a missing device.

2. **Compile.** From the `code/` directory:
   ```bash
   arduino-cli compile \
     --fqbn "teensy:avr:teensy41:usb=serial2,speed=600,opt=o1lto,keys=en-us" \
     --output-dir ../ArduinoOutput \
     src/PhoenixSketch/PhoenixSketch.ino
   ```
   Compiles take a while; run with a generous Bash timeout (e.g. 600000 ms / 10 min). If compilation fails, surface the first error and stop — do not flash a broken build.

3. **Flash.** If the port discovered in step 1 differs from `/dev/ttyACM0`, use that one. From the `code/` directory:
   ```bash
   arduino-cli upload \
     -p /dev/ttyACM0 \
     --fqbn "teensy:avr:teensy41:usb=serial2,speed=600,opt=o1lto,keys=en-us" \
     --input-dir ../ArduinoOutput \
     src/PhoenixSketch/PhoenixSketch.ino
   ```
   The Teensy loader may prompt the user to press the physical button on the board — mention this if upload appears to hang.

4. **Report.** Summarise in 1–2 lines: whether compile succeeded, whether upload succeeded, and any notable warnings.

## Args

- `compile-only` — run step 2 only; skip the flash step. Useful when no board is connected or the user just wants to verify the build.
- `flash-only` — skip compile and re-flash whatever is already in `../ArduinoOutput`. Only valid if that directory contains a recent build for this sketch; otherwise fall back to the full compile+flash.

## Notes

- Always run from `code/` so the relative paths in `arduino.json` resolve correctly.
- Do **not** invoke this skill to build the unit tests — those use CMake under `code/test/build/` and are a separate workflow.
- The `code/build/` directory is the CMake test build; the firmware artifacts go in `ArduinoOutput/` at the repo root.
