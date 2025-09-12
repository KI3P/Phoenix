# CLAUDE.md - Phoenix SDR Radio Project

## Project Overview

This is the **Phoenix SDR (Software Defined Radio)** project - firmware for a Teensy 4.1-based amateur radio transceiver. The project implements a complete SDR radio system using state machines for hardware control and real-time digital signal processing.

## Architecture

### Core Components
- **Hardware Platform**: Teensy 4.1 microcontroller
- **Language**: C/C++ with Arduino framework (`.ino`, `.cpp`, `.h` files)
- **State Machines**: Generated from UML diagrams using StateSmith
- **Real-time DSP**: OpenAudio library for signal processing
- **Testing**: Google Test framework with comprehensive unit tests

### Key Features
- Dual VFO (Variable Frequency Oscillator) operation
- SSB (Single Sideband) and CW (Morse Code) modes
- Real-time digital signal processing
- State machine-controlled hardware management
- CAT (Computer Aided Transceiver) control interface
- Comprehensive test coverage with mocking framework

## Code Structure

### Main Source Directory: `code/src/PhoenixSketch/`
- **PhoenixSketch.ino**: Main Arduino sketch entry point
- **Loop.cpp/h**: Main program loop implementation
- **ModeSm.cpp/h**: Radio mode state machine (generated code)
- **UISm.cpp/h**: User interface state machine (generated code)
- **RFBoard.cpp/h**: RF hardware control and VFO management
- **Tune.cpp/h**: Frequency tuning and VFO control with state machine
- **DSP*.cpp/h**: Digital signal processing modules
- **CAT.cpp/h**: Computer control interface
- **FrontPanel*.cpp/h**: Physical front panel interface
- **SDT.h**: Main system definitions and configuration

### Test Directory: `code/test/`
- **CMakeLists.txt**: CMake build configuration for tests
- **\*_test.cpp**: Comprehensive unit tests for all modules
- **\*_mock.cpp**: Mock implementations for testing
- **Arduino.h/Arduino_mock.cpp**: Arduino framework mocking

## Build System

### Arduino IDE Build
```bash
# Install Arduino IDE 2.3.6+ with Teensyduino
# Install required libraries:
# - Adafruit MCP23017 (via Library Manager)
# - OpenAudio (manual install from GitHub)
```

### Unit Testing (Google Test + CMake)
```bash
# Create build directory
mkdir code/test/build

# Build and run tests
cd code/test/build
cmake ../ && make
ctest --output-on-failure
```

## Key Development Patterns

### State Machine Architecture
The project uses **StateSmith**-generated state machines for hardware control:
- `ModeSm`: Controls radio operating mode (SSB_RECEIVE, SSB_TRANSMIT, CW_TRANSMIT, etc.)
- `UISm`: Manages user interface states
- State transitions are event-driven and deterministic
- All hardware state changes go through the state machines

### Frequency Control State Machine
The `Tune.cpp` module implements a frequency control state machine with three states:
- **TuneReceive**: Sets SSB VFO for RX operation
- **TuneSSBTX**: Sets SSB VFO for TX operation  
- **TuneCWTX**: Sets CW VFO with tone offset for TX operation

### Real-time Constraints
- Main loop must complete within 10ms to avoid audio buffer overflows
- Interrupt-driven button handling with event queuing
- State-based hardware control ensures deterministic timing

### Testing Strategy
- Extensive mocking of Arduino and hardware interfaces
- Unit tests cover all major modules and state transitions
- Mock objects for Si5351 VFO, GPIO, and audio interfaces
- Test-driven development approach encouraged

## Working with This Codebase

### When Making Changes
1. **Run existing tests first**: `cd code/test/build && ctest`
2. **Follow state machine patterns**: Hardware changes should go through state machines
3. **Add unit tests**: All new functionality should have corresponding tests
4. **Respect real-time constraints**: Keep main loop execution under 10ms
5. **Use mocking for hardware**: Tests should use provided mock interfaces

### Common Tasks
- **Adding new DSP functions**: Follow patterns in `DSP_*.cpp` files
- **Modifying state machines**: Update `.drawio` diagrams and regenerate with StateSmith
- **New hardware interfaces**: Create a `.cpp` and a `.h` file to hold hardware-specific code (look at `RFBoard.cpp` for an example) with corresponding tests
- **Frequency/tuning changes**: Work with the tune state machine in `Tune.cpp`

### Build Commands
```bash
# Arduino build: Use Arduino IDE with Teensyduino
# Unit tests:
cd code/test/build
cmake ../ && make && ctest --output-on-failure

# Lint/format (if available):
# Follow existing code style in the codebase
```

### Test Commands
```bash
# Run all tests
ctest --output-on-failure

# Run specific test suite
ctest -R RFBoard --output-on-failure

# Run with verbose output
ctest -V
```

## Dependencies

### Runtime Dependencies
- Teensyduino (Teensy 4.1 Arduino framework)
- OpenAudio_ArduinoLibrary (real-time audio processing)
- Adafruit MCP23017 (I2C GPIO expander)

### Development Dependencies
- Arduino IDE 2.3.6+
- StateSmith (for state machine code generation)
- CMake (for unit testing)
- Google Test (automatically downloaded)

### Optional Dependencies
- draw.io (for editing state machine diagrams)

## Hardware Target
- **Primary**: Teensy 4.1 microcontroller (ARM Cortex-M7)
- **Audio**: Real-time 48kHz/96kHz/192kHz sampling
- **RF**: Si5351 clock generator for VFO control
- **Interface**: MCP23017 I2C GPIO expanders for front panel

This is a sophisticated embedded project combining real-time signal processing, state machine control, and comprehensive testing in the amateur radio domain.