# This repository

This is a new software approach, called Phoenix, for the T41-EP radio running V12 hardware. It is a ground-up rewrite of the T41 software, which had been authored by dozens of people over the years. I have listed the known authors that left their call signs in the code in the file `code/src/PhoenixSketch/Contributors.txt` -- please let me know if I've missed anyone!

![](images/T41_radio.png)

# T41-EP overview

The T41-EP Software Defined Transceiver (SDT), originally designed by Al Peter-AC8GY and Jack Purdum-W8TEE, is a 20W, HF, 7 band, CW/SSB Software Defined Transceiver (SDT) with features like 192kHz spectrum display bandwidth, ALP CW decoder, Bode Plots. The T41-EP is a self-contain SDT that does not require an external PC, laptop, or tablet to use. Al and Jack wrote a book, available on [Amazon](https://www.amazon.com/Digital-Signal-Processing-Software-Defined/dp/B0F5BDQZW3), describing the theory and operation of the T41-EP. The [project's website](https://t41sdrtransceiver.wordpress.com/) gives an overview of the project and its history.

Some of the features of the T41 V12 hardware running the Phoenix firmware:

* Operation across all amateur radio bands from 160m to 4m, plus a general-coverage tuning range.
* Controllable receive and transmit power levels in 0.5 dB steps.
* Audio gain control via a variety of algorithms.
* Noise reduction via a variety of algorithms.
* Receive and transmit audio equalizers.
* Automated calibration routines for receive IQ balance, transmit IQ balance, transmit carrier nulling, and PA output power.
* Selectable sample rate: 192 or 176.4 ksps, chosen at run time and persisted, with the audio filters redesigned on each change so their frequencies do not move.
* Digital mode: bi-directional USB audio for FT8 and the other WSJT-X modes, with no external sound card and no resampling.
* CAT control.

The T41-EP is a fully open-source radio. This repository hosts the transceiver software, licensed under GPLV3 (see LICENSE file). The hardware designs are hosted on Bill-K9HZ's [GitHub repository](https://github.com/DRWJSCHMIDT/T41/tree/main/T41_V012_Files). The primary forum for discussions on the T41-EP radio is on [Groups.io](https://groups.io/g/SoftwareControlledHamRadio/topics).

The EP stands for Experimenter's Platform because the T41-EP is designed around 5 small printed circuit boards (100mm x 100mm) that can be easily swapped for boards of your own design. Because the T41-EP project is completely Open Source, you have complete access to the C/C++ source code that controls the T41-EP as well as the KiCad design files, schematics, and Gerber files. 

The hardware design files for the V12 radio modules can be found at the following links:

* [Main board](https://github.com/DRWJSCHMIDT/T41/tree/main/T41_V012_Files/T41_V012_PCBs/T41_V012_PCBs_KiCad/T41-main-board-V012)
* [RF board](https://github.com/DRWJSCHMIDT/T41/tree/main/T41_V012_Files/T41_V012_PCBs/T41_V012_PCBs_KiCad/T41-RF-board-V012)
* [BPF board](https://github.com/DRWJSCHMIDT/T41/tree/main/T41_V012_Files/T41_V012_PCBs/T41_V012_PCBs_KiCad/T41-BPF-filter-board)
* [Front panel switch board](https://github.com/DRWJSCHMIDT/K9HZ/tree/main/K9HZ_Front_Panel_Boards)
* [Front panel encoder boards](https://github.com/DRWJSCHMIDT/K9HZ/tree/main/K9HZ_Encoder_Boards)
* [LPF module](https://github.com/DRWJSCHMIDT/K9HZ/tree/main/K9HZ_LPF_Module)
* [20W amplifier module](https://github.com/DRWJSCHMIDT/K9HZ/tree/main/K9HZ_20W_PA)

The latest version (V12.6) of the bare PCBs are available for less than $5 each on the [discussion forum](https://groups.io/g/SoftwareControlledHamRadio) by contacting Bill K9HZ. If you prefer a partially-assembled kit,  Justin AI6YM sells them on his [website](https://ai6ym.radio/t41-ep-sdt/).


## Screenshots

The home screen should look familiar to users of previous versions of the code, with some change in the layout.

![](images/Home_window.png)

The menu system has been completely rewritten.

![](images/Menu_window.png)

As have the calibration routines. The receive IQ calibration can now automatically tune across all the bands with a single button press.

![](images/RXIQ_window.png)

A block diagram of the V12 radio hardware is shown below.

![](images/T41_V12_block_diagram.png)

The mapping of the buttons to various functions is defined in `code/src/PhoenixSketch/Config.h`. The default mapping is:

![](images/Home_screen_button_map.png)

# Version History

## V1.4.0 Release Notes

### Major New Features

**Digital Mode: USB Audio for FT8 and the Other WSJT-X Modes**
A third operating mode alongside SSB and CW. In receive the demodulated audio is streamed to the PC over USB audio (and still to the speaker); in transmit the audio source is the PC instead of the microphone. Enter it by cycling the mode button (SSB → CW → DIGITAL) or with the new `DG` CAT command; WSJT-X keys the radio over CAT in the normal way.

No resampler and no modified Teensy core file are needed, because at 176.4 ksps the receive chain's Fs/4 audio tap *is* 44,100 Hz — the fixed rate of the Teensy USB audio endpoint — and the audio library's graph clock is exactly 4× the USB block rate. Entering digital mode therefore forces 176.4 ksps and leaving restores the previous rate. `USBAudio.cpp` keeps the stock `AudioInputUSB`/`AudioOutputUSB` objects but drives them from a 4:1 pacer rather than from the I²S-clocked audio graph.

This mode requires the **Serial + MIDI + Audio** USB type (see the Arduino build settings below). In a build without a USB audio interface the firmware compiles and runs exactly as before, with the DIGITAL leg simply absent from the mode cycle.

**Selectable Sample Rate (192 or 176.4 ksps)**
The radio's sample rate can now be changed while it is running, from the menu or over CAT with the new `SR` command, and the choice is persisted across power cycles. `ChangeSampleRate()` reconfigures the I²S clock and rebuilds the entire DSP chain.

**Audio Filters Designed at Run Time**
This change makes a switchable sample rate possible. In prior versions several audio-rate filters shipped as coefficient tables designed offline for one audio rate (24 ksps, i.e. 192 ksps decimated by 8). Changing the sample rate without changing the filters would lead to unwanted changes in these filter edges. 

The CW audio filters, the 14 equaliser cells, the CW decoder input filter, and the two transmit stages either side of the Hilbert transform are now generated from an analog design specification on every rate change. The filter families are unchanged and the original tables are kept as test fixtures so that the generator code's output filters can be checked against them. Filters whose corners are specified as a fraction of the sample rate — the decimation, interpolation, Hilbert and zoom-FFT stages — are unchanged.

**Rapid-Tune Audio Mute and Spectrum Freeze**
Spinning the Center Tune or Fine Tune encoder quickly reprograms the VFO on every pass of the main loop, producing audible glitches and a jerky spectrum. With `MUTE_ON_RAPID_TUNE` enabled (the default), rapid tuning is detected and the audio is muted and the spectrum/waterfall redraw paused until tuning stops. Comment out the `#define` in `Config.h` to restore the old behaviour.

**CAT Control of the DSP Settings and Digital Mode**
Seven new commands, bringing the table to 32. Four control the DSP: `SR` (sample rate), `CF` (CW audio filter index), `EQ` (receive equaliser cell) and `FL` (DSP filter low cut — the mirror of the existing `FW` high cut); these were added so the filters could be measured on real hardware using a script. Three serve digital mode: `DG` (enter/leave), `DR` (USB receive level) and `DS` (read-only transmit-path counters). None of the seven is a standard CAT keyword.

**Hardware-in-the-Loop Filter Test Suites**
`code/tools/filter_hil/` is an automated measurement suite for the receive DSP filters on the actual radio, at every sample rate, using an Analog Discovery 2 to inject a quadrature tone into the I/Q inputs and read the demodulated audio off the speaker.

`code/tools/tx_filter_hil/` is the transmit counterpart, capturing both exciter I/Q outputs synchronously so that the audio passband and the opposite-sideband suppression come from one sweep. 

**LLM-Maintained Knowledge Wiki**
A new `wiki/` directory holds an interlinked reference covering the firmware modules, the DSP theory behind them, the hardware platform, and the development roadmap. Start at `wiki/index.md`.

### Bug Fixes

- **Sideband selection was inverted on bands whose default is the other sideband.** `SetModulation()` wrote `bands[].mode` as well as `ED.modulation[]`, but `bands[].mode` is the band *default* against which `InitFilterMask()` measures its passband mirroring. Making the default agree with the request switched the mirroring off, so on 40 m selecting USB actually tuned the radio LSB while the display went on saying USB. `MD_read`/`IF_read` read the same field and so reported the band default rather than the mode in use. This is the other half of the `MD` fix listed below: the earlier fix made `MD` write `ED.modulation[]`, this one stops it from also writing the band default that the mirroring is measured against. The front-panel DEMODULATION button was never affected — it only ever wrote `ED.modulation[]`.
- **`TX;` without a parameter.** The TS-480 spec makes P1 optional, and Hamlib picks between `TX;`, `TX0;` and `TX1;` according to the configured PTT type, so the radio keyed for some clients and silently refused for others. All forms of `TX` and `RX` are now accepted.
- **Digital-mode audio buffering** (found on the bench). The USB play queue was never prefilled, giving 147 dropouts per 12 s of receive; and the transmit record queue was started for all of digital mode but drained only while transmitting, so it sat at its 209-block ceiling and held roughly 418 of the 500 audio blocks. Bounding it improved transmit spurs by 47 dB — and, because it had been starving the shared audio pool, improved receive second-harmonic distortion by 17.5 dB and the receive noise floor by 16 dB.
- **Three long-standing CAT bugs.** `MD` set the band's mode but never `ED.modulation[]`, and because `InitFilterMask()` compares the two and treats a difference as deliberate, `MD` did not change the demodulator but mirrored the receive passband. `MD1`/`MD2` could not escape CW mode, so a CAT client could enter CW and never leave. `FW;` was unreadable because the command declared equal set and read lengths, which makes the read handler unreachable. `MD4` now selects AM, which was previously unreachable.
- **NaN values leaking out of uninitialised DMAMEM.** `ResetDSP()` existed but was never actually called during signal-processing initialisation, so the DMAMEM arrays could be read before being zeroed.
- **The blue filter bar** on the spectrum display and the band edges on the audio display are drawn in the correct places (reported by Wolfgang Fiebig).
- **Sidetone volume** can be adjusted again (reported by Wolfgang Fiebig).
- **The AM DC blocker's filter state** was a file-scope static shared across all three `ReceiveFilterConfig` instances; it is now per-instance.
- **`ApplyEQBandFilter` scrubbed** the receive equaliser's filter state even when running on the transmit path.

### Under-the-Hood Changes

- Frequency units renamed throughout from dHz to **cHz** (centi-Hz, Hz × 100), which is what the code always actually used.
- Faster encoder handling, and spectrum artifacts while tuning fixed.
- `BuildInfo.h` is stamped automatically by a pre-commit hook, which is itself versioned in the repository so every clone gets it.
- Unit tests grown from 699 to 779, including a suite that checks each generated filter against the reference table it replaced and that every corner holds across a rate change, and a suite covering the digital-mode state transitions and CAT commands.
- Two new persisted settings, `digitalDriveLevel` and `digitalRxLevel`, with menu entries under Microphone options ("USB drive", "USB RX level").

## V1.3 Release Notes

### New Features

**General Coverage Receive Band** 
A receive-only "GEN" band covering 200 kHz to 80.8 MHz *(contributed by Michael, dj2mg)*. The band table gained a `BandType` (`HAM_BAND` / `MISC_BAND`), and `IsTxAllowed()` now guards all four transmit entry transitions in the mode state machine, so the radio cannot be keyed outside an amateur band. Low-pass filter selection is handled appropriately for non-ham bands.

**Faster Spectrum and Waterfall**
More efficient waterfall operation *(contributed by tmr4)*, and the spectrum trace is drawn in `NCHUNKS` column groups, one group per pass of the main loop. Reducing `NCHUNKS` from 8 to 6 raised the spectrum refresh rate from roughly 11.7 to 15.6 fps. The audio spectrum and the waterfall are each updated every second spectrum frame, phase-offset from one another, to keep the per-loop cost inside the budget.

**Serial Time Sync**
Send `T` followed by a 10-digit Unix UTC timestamp and a newline to the debug serial port (115200) to set both the Teensy's coin-cell-backed hardware RTC and the TimeLib clock. WSJT-X requires the clock to be within ±1 second of UTC.

**Filter Band Edge Control**
The Filter button on the front panel now toggles which band edge the Filter encoder adjusts, in addition to the existing behaviour using the Filter encoder's own button. The blue band indicator is drawn in the right place, and adjusting a band edge now catches and prevents invalid conditions.

### Bug Fixes

- **Noise reduction dropped audio after a cold power cycle**, caused by uninitialised DMAMEM.
- **The BIT screen misreported dual VFO presence.**

### Under-the-Hood Changes

- A GitHub Actions workflow compiles the firmware on every pull request.
- GPL headers added to all source files.
- `BUFFER_SIZE` renamed to `USB_BUFFER_SIZE` to avoid a namespace conflict with other libraries.
- Rotary encoder tuning tweaks *(contributed by Tom Metty, AJ8X)*.
- ArduinoJson documented as a build dependency *(contributed by AnttiVL)*.
- `Contributors.txt` moved to `code/src/PhoenixSketch/`.

## V1.2 Release Notes

### New Features

**Optional Analog SWR Measurement**
A new compile-time option `USE_ANALOG_SWR` enables SWR measurement using Teensy ADC pins 26 (forward) and 27 (reverse) instead of the AD7991 digital ADC. This provides an alternative for builders who prefer analog measurement or don't have the AD7991 chip. Enable this option by uncommenting `#define USE_ANALOG_SWR` in `Config.h`.

**SWR/Power Display Pane**
A new home screen pane displays real-time SWR and forward power during transmit:
- Shows SWR value (1.0-10.0) and forward power in watts
- Updates at 4 Hz during transmit
- Text displays in red during active transmit, white during receive
- Automatically detects transmit activity based on SWR measurement timing

### Under-the-Hood Changes

- Hardware register expanded from 32 to 64 bits to support TXVFOBIT for dual VFO tracking
- Display code optimized to skip spectrum/waterfall updates during SSB transmit mode
- Added `SI5351_DUAL_VFO_ADDR` constant (0x61) for dual VFO hardware configuration
- New `ReadSWRLastUpdateMs()` API function for tracking SWR measurement timing
- Added `DIRECT_COUPLED_TX` compile-time option (disabled by default)
- Added spare button mapping slot in Config.h
- Spectrum draw faster: `ShowSpectrum()` now back-buffers the yellow trace on LAYER2 and publishes the whole spectrum rect to LAYER1 in a single hardware `BTE_move` per sweep instead of erasing and redrawing every bin individually. Median `DrawSpectrumPane` cost drops from ~5 ms to ~2 ms per chunk; total CPU share falls from ~51% to ~45%, freeing budget for signal processing. Trace is clipped to leave the top 20 rows free for the bandwidth/scale labels. Baseline + Phase 1 measurements and plots are in `code/docs/DrawDisplay_Timing_Baseline.md`.

## V1.1 Release Notes

### Major New Features

**Dual VFO Architecture**
The radio now supports separate RX and TX VFOs using independent Si5351 clock outputs. This enables monitoring the transmit signal on the receiver during SSB transmission - you can now see your own transmitted spectrum on the display in real-time. The system auto-detects whether dual VFO hardware is present and adjusts operation accordingly.

**Automatic Calibration System**
Three new state machine-driven auto-calibration routines have been added:
- **RX IQ Calibration** - Automatically optimizes receive IQ balance to minimize opposite sideband leakage
- **TX IQ Calibration** - Auto-tunes transmit IQ parameters for optimal sideband suppression
- **TX Carrier Nulling** - Automatically minimizes carrier leakthrough using DC offset correction

All three use a gradient-based optimization algorithm that searches for optimal settings without requiring external test equipment. The calibration values are stored per-band.

**Desktop Radio Simulator**
A new SDL-based simulator (`radio_simulator`) allows testing the complete radio firmware on a desktop PC. Features include:
- Full 800x480 display emulation
- Keyboard mappings for all front panel buttons and encoders
- Selectable audio sources: computer input, two-tone test, single-tone, or RX IQ test signals
- Real-time DSP processing with audio output to computer speakers
- Configuration file read/write to persistent storage

### Under-the-Hood Changes

- Hardware register expanded from 32 to 64 bits to accommodate dual VFO state tracking
- VFO API renamed from `SetSSBVFOFrequency()` to `SetRXVFOFrequency()`/`SetTXVFOFrequency()`
- New hardware states added: `RFCalTransmitIQSingleVFO`, `RFCalTransmitIQDualVFO`, `RFCalTransmitCarrier`
- Improved test infrastructure with expanded mocks for OpenAudio, LittleFS, and ArduinoJson
- Added transmit chain unit tests
- Buffer print diagnostic added for debugging hardware register state

# File Organization

## Source Code Locations

The main Arduino sketch and all accompanying source files are located here:

* **Teensy Source Code**: `code/src/PhoenixSketch/`

The comprehensive unit tests, mocking functions, and their build files are located here:

* **Unit tests**: `code/test`

The same directory also builds the **desktop radio simulator** (`RadioSimulator_main.cpp` → the
`radio_simulator` target), which runs the firmware on a PC with an SDL-emulated display.

Useful sketches for testing the boards as you build them are here:

* **Board tests**: `code/src/BoardTest`

The sketch for the ATTiny85 in the main board power control block is here:

* **ATTiny85 power control**: `code/src/ATTiny85_On_Off`

Host-side Python tooling for measuring the radio on the bench — mostly hardware-in-the-loop test
suites driven over CAT with an Analog Discovery 2 — is here, each subdirectory with its own README:

* **Bench tools**: `code/tools` (including `filter_hil/` and `tx_filter_hil/`, which measure the
  receive and transmit DSP filters on the real radio at both sample rates)

An LLM-maintained knowledge base covering the firmware, the DSP theory behind it, the hardware
platform, and the development roadmap is here:

* **Knowledge wiki**: `wiki/` — start at `wiki/index.md`

## Useful Diagrams (.drawio files)

These diagrams can be edited with draw.io. Some are used to generate state machine code with StateSmith:

* **code/src/PhoenixSketch/ModeSm.drawio**: StateSmith radio mode state machine - controls transmit/receive states, mode selection (SSB/CW), and hardware transitions
* **code/src/PhoenixSketch/UISm.drawio**: StateSmith user interface state machine - manages display states, menu navigation, and screen layouts
* **images/state_machine_architecture.drawio**: High-level state machine architecture diagram showing how the state machines interact
* **code/docs/window_panes.drawio**: UI window pane layout designs and screen organization, button functions
* **code/docs/HAL_diagrams/T41_V12_board_api.drawio**: Hardware abstraction layer API documentation for the T41 V12 board interfaces

# Architecture overview

Phoenix SDR is built around a state machine-driven architecture that ensures deterministic hardware control. Events like button presses, CAT commands, or encoder turns are placed in an interrupt buffer and handled at a predictable point in the code, preventing state corruption. The code implements hardware abstraction for testability and modularity.

1. **Separation of Concerns**: Hardware control, signal processing, and display are isolated into separate modules with clear interfaces. Hardware interfaces (RFBoard, LPFBoard, BPFBoard) provide clean APIs, allowing mocking for unit tests.
2. **State Machine Control**: All hardware state changes flow through state machines (ModeSm, UISm, HardwareSm) ensuring deterministic, predictable behavior.
3. **Separated Display**: Display code is separated from the rest of code and reads the global state but never modifies it. As a demonstration of the display code separation, it can be disabled by commenting out a single line in `loop()` and the radio can operate without the display.
4. **Testing and Debugging**: An extensive set of unit tests ensure that changes to the code don't cause unexpected subtle bugs. The test framework enables you to use software debuggers to exercise the radio code, greatly speeding up the process of finding and fixing most bugs.

## State machines

The behavior of the radio is controlled by a set of state machines. Hardware interrupts, like button presses, are handled by the main loop and used to dispatch events to the Mode and UI state machines, as well as update the shared global configuration settings. The Mode State Machine drives the hardware state machine (`HardwareSm.cpp`), which is hand-written rather than generated; it both configures the hardware and owns the VFO frequency state (`HandleTuneState`). It in turn drives the four generated calibration state machines. The architecture of the state machines is shown in the diagram below.

![](images/state_machine_architecture.png)

The state of the radio hardware is controlled by a radio mode state machine. Using a state machine to control the hardware ensures that all hardware is always in a known configuration state. The state machine UML diagram is found in the file `code/src/PhoenixSketch/ModeSm.drawio`, and is shown below.

![The state machine that controls the radio operating mode](images/RadioModeSm-design.png)

Similarly, we use a state machine to control the user display. This helps to logically separate the display from the rest of the radio operation, making it more portable and extensible.

![The state machine that controls the radio operating mode](images/UISm-design.png)

State machines can be written entirely in C code, but it's easier to understand how the state machine operates through a drawing. We draw the state machines in a graphical environment and then generate the C code that implements the state machine using [StateSmith](https://github.com/StateSmith/StateSmith). The state machines shown above are examples of this.

## Control flow

The software runs in a loop as shown in the diagram below. It performs three major functions:

1. Handle interrupt events: if an interrupt was registered by, for example, a button being pressed, then pass the appropriate event on to the state machines.
2. Perform the appropriate signal processing, based on the current radio mode.
3. Update the display, based on the current UI state.

Then go back to step 1 and repeat. This loop should take at most 10ms to execute in order to avoid buffer overflows in the audio processing software.

![Program flow diagram](images/Program_flow_v1.png)

### Handling button presses

We don't want button presses to change the hardware state at random, unspecified times. In order to control the timing of when we respond to button presses, we attach the buttons to interrupts that set an interrupt event register. This register is checked and cleared only once in the main loop. 

## Remote control

Built with the **Dual Serial** USB type, the radio provides two serial interfaces over USB. The first, at a baud rate of 115200, prints debug messages. The second, at a baud rate of 38400, implements CAT control with a partial implementation of the Kenwood TS-480 CAT Interface.

Built with **Serial + MIDI + Audio** — the USB type digital mode needs — Teensyduino offers only one CDC port, so CAT moves to that first (and only) port and the debug output is compiled out to keep it from corrupting the CAT stream. Point your CAT client at the first port in that configuration.


# Build environment

## Arduino

This code has been tested with the Arduino IDE version 2.3.6. Configure your Arduino IDE to use the Teensyduino library following the instructions [here](https://www.pjrc.com/teensy/td_download.html). 

Install the following libraries via the Arduino Library Manager:

* Adafruit MCP23017 Arduino Library, by Adafruit (v2.3.2) (note: install with dependencies)
* ArduinoJson, by Benoit Blanchon (v7.4.3)

Some libraries need to be installed manually. The manual process is:

1. Go to the provided GitHub link for the library and download the library as a zip by clicking Code -> Download ZIP.
2. Import it into the Arduino 2 IDE by clicking Sketch -> Include Library -> Add .ZIP library, and then selecting the file you just downloaded.

The libraries to install using this process are:

* OpenAudio: [https://github.com/chipaudette/OpenAudio_ArduinoLibrary](https://github.com/chipaudette/OpenAudio_ArduinoLibrary)

Configure the Arduino IDE to compile with the following settings:

* USB type: **Serial + MIDI + Audio** if you want digital mode (FT8 etc.), otherwise **Dual Serial**. See the note under Remote control above — the choice determines which port carries CAT.
* 600 MHz
* Fast with LTO

You should see memory usage in the region of the figures below when compilation is complete. They
are indicative rather than exact — they move with every release, and with your `Config.h` options:
```
Memory Usage on Teensy 4.1:
  FLASH: code:302828, data:78444, headers:8868   free for files:7736324
   RAM1: variables:139744, code:299672, padding:28008   free for local variables:56864
   RAM2: variables:302528  free for malloc/new:221760
```

## StateSmith

[StateSmith](https://github.com/StateSmith/StateSmith) is used to generate state machine code from UML diagrams. The diagrams are drawn in draw.io, a graphical diagram editor. draw.io is available in an online version and a desktop version; use the Desktop version which you can download [here](https://www.drawio.com/).

You only need to install the StateSmith binary if you want to update the state machines. The code will compile without StateSmith installed.

Install the StateSmith binary using [these](https://github.com/StateSmith/StateSmith/wiki/CLI:-Download-or-Install) instructions. For simplicity, we recommend using the pre-built binaries rather than building it from source.

## Google Test

The test framework isn't needed to compile and run the code. If you want to modify the code, using the test framework is highly recommended as it will help you discover when your changes break the code.

The Test Driven Development framework we use is [Google Test](https://google.github.io/googletest/). You don't need to install it -- it is downloaded as part of the make process. You need to have [cmake](https://cmake.org/download/) installed. 

The first time you run the unit tests, start in the Phoenix directory and run the following command to create a build directory:

```bash
mkdir code/test/build
```

Then, to build and run the unit tests, do the following:

```bash
cd code/test/build
cmake ../ && make
ctest --output-on-failure
```

This will compile and run the unit test programs. You should see an output that looks something like this:

```bash
...
776/779 Test #776: PowerCalibrationTest.FitPowerCurve_RealWorldData ..............................   Passed    0.01 sec
        Start 777: PowerCalibrationWalkthroughTest.CompleteCalibrationAllBands
777/779 Test #777: PowerCalibrationWalkthroughTest.CompleteCalibrationAllBands ...................   Passed    1.75 sec
        Start 778: PowerCalibrationWalkthroughTest.RemeasuringDataPoints_StateRemainsCorrect
778/779 Test #778: PowerCalibrationWalkthroughTest.RemeasuringDataPoints_StateRemainsCorrect .....   Passed    0.54 sec
        Start 779: PowerCalibrationWalkthroughTest.StateTransitions_ResetBehavior
779/779 Test #779: PowerCalibrationWalkthroughTest.StateTransitions_ResetBehavior ................   Passed    0.60 sec

100% tests passed, 0 tests failed out of 779

Total Test time (real) =  91.50 sec
```

