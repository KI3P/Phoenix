---
name: flash
description: Compile and flash the PhoenixSketch firmware to the T41 radio (Teensy 4.1). Use when the user wants to build, compile, upload, or flash the production firmware to the radio. Requires the radio connected via USB at /dev/ttyACM0.
when_to_use: "flash radio", "compile firmware", "upload firmware", "build and flash", "program the radio", "flash the Teensy"
argument-hint: "[--compile-only]"
arguments: [compile_only]
allowed-tools: Bash(arduino-cli*) Bash(ls /dev/ttyACM*) Bash(ls /dev/ttyUSB*)
user-invocable: true
---

# Flash PhoenixSketch to T41 Radio

Compile the PhoenixSketch Arduino firmware and upload it to the Teensy 4.1 via USB.

## Build configuration

From `code/.vscode/arduino.json`:
- **Sketch**: `code/src/PhoenixSketch/PhoenixSketch.ino`
- **Board**: `teensy:avr:teensy41`
- **Settings**: Dual Serial, 600 MHz, Fast with LTO (`usb=serial2,speed=600,opt=o1lto,keys=en-us`)
- **Output dir**: `ArduinoOutput/` (repo root)
- **Port**: `/dev/ttyACM0`

## Steps

### 1. Check the radio is connected

```bash
ls /dev/ttyACM*
```

If `/dev/ttyACM0` is absent, the radio is not connected or not powered on. Stop and tell the user.

### 2. Compile

Run from the `code/` directory:

```bash
cd /home/oliver/Sync/Ham/T41/Software/Phoenix/code && \
arduino-cli compile \
  --fqbn "teensy:avr:teensy41:usb=serial2,speed=600,opt=o1lto,keys=en-us" \
  --libraries lib \
  --output-dir ../ArduinoOutput \
  src/PhoenixSketch/PhoenixSketch.ino
```

`--libraries lib` adds the in-tree `code/lib/` directory (which holds
the Phoenix-owned forked libraries, e.g. `RA8875_DMA`) to arduino-cli's
search path.

Expected memory report on success (approximate):
```
FLASH: code:~287KB  free for files:~7.7MB
 RAM1: variables:~127KB  free for local variables:~102KB
 RAM2: variables:~300KB  free for malloc/new:~224KB
```

If compilation fails, report the full error output to the user and stop — do not attempt to upload.

### 3. Upload (skip if `--compile-only` was passed)

```bash
arduino-cli upload \
  --fqbn "teensy:avr:teensy41:usb=serial2,speed=600,opt=o1lto,keys=en-us" \
  --port /dev/ttyACM0 \
  --input-dir /home/oliver/Sync/Ham/T41/Software/Phoenix/ArduinoOutput \
  /home/oliver/Sync/Ham/T41/Software/Phoenix/code/src/PhoenixSketch/PhoenixSketch.ino
```

A successful upload ends with the Teensy rebooting and the radio display restarting.

## Reporting results

- **Success**: Report the memory usage numbers from the compile step, confirm upload succeeded.
- **Compile error**: Show the compiler error message. Common causes: missing library, syntax error.
- **Upload error**: The Teensy loader may need the reset button pressed — tell the user to press the white button on the Teensy board, then retry the upload command.
- **Port not found**: Tell the user to check USB connection and that the radio is powered.
