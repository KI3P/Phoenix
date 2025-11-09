# Contributing to Phoenix SDR

Thank you for your interest in contributing to the Phoenix SDR project! This guide will help you understand the project structure, coding conventions, and architecture patterns to ensure your contributions align with the existing codebase.

## Table of Contents

1. [Quick Reference Summary](#quick-reference-summary)
2. [Project Structure](#project-structure)
3. [File Naming Conventions](#file-naming-conventions)
4. [Coding Style](#coding-style)
5. [Architecture Rules](#architecture-rules)
6. [Adding Display Screens](#adding-display-screens)
7. [Testing Guidelines](#testing-guidelines)
8. [State Machine Modifications](#state-machine-modifications)
9. [Pull Request Process](#pull-request-process)

## Quick Reference Summary

### Critical Architecture Rules
1. **Central Hardware State**: ALL hardware state stored in single 32-bit `hardwareRegister` - use bit manipulation macros from `SDT.h`. Add another hardware register if more bit space is needed.
2. **State Machine Control**: ALL hardware transitions go through state machines (`ModeSm`, `UISm`, `Tune`) - never change hardware directly.
3. **Hardware Module Encapsulation**: Small public header (~50-100 lines), large private implementation, define an API that abstracts away hardware-specific details.
4. **Display System**: Display functions read state, render display, never modify external state (except UPDATE/CALIBRATE states). Display changes happen exclusively through the DrawDisplay() function.
5. **Pane-Based Display**: Divide the window into independent panes with position, draw function pointer, and stale flag for selective updates.
6. **Register Caching**: Always cache register values to avoid unnecessary I2C writes - check before writing.

### File Naming Conventions
- **Paired Files**: `ModuleName.h` + `ModuleName.cpp`
- **Hierarchy**: `ParentModule_Submodule.cpp` (e.g., `MainBoard_DisplayHome.cpp`)
- **State Machines**: `*Sm.cpp/h` (do not edit generated code manually)
- **Tests**: `*_test.cpp`, **Mocks**: `*_mock.cpp`

### Coding Style Quick Reference
- **Functions**: CamelCase with uppercase start (`InitializeModule()`)
- **Variables**: camelCase with lowercase start (`moduleInitialized`)
- **Constants**: UPPERCASE_WITH_UNDERSCORES (`MAX_ATTENUATION_VAL`)
- **Include Units**: `frequency_dHz`, `attenuation_dB`, `duration_ms`
- **Indentation**: 4 spaces (not tabs)
- **Braces**: Opening brace on same line `void Func(void){`
- **Comments**: Doxygen-style for public functions, mark private functions with `PRIVATE:`

### Testing Requirements
- **All new code** must include unit tests
- **Coverage**: initialization, valid inputs, boundary conditions, invalid inputs, state transitions, interactions with other code
- **Build & Run**: `cd code/test/build && cmake ../ && make && ctest --output-on-failure`
- **Use Mocks**: Test hardware modules with provided mock implementations

### State Machine Rules
- **Never edit** generated `.cpp/.h` files directly
- **Modify** the `.drawio` UML diagrams instead
- **Regenerate** using StateSmith: `ss.cli run -h`
- **Dispatch Events**: Use `ModeSm_dispatch_event(&ModeSm, EventId)`

### Pull Request Checklist
- [ ] All unit tests pass
- [ ] New functionality has unit tests
- [ ] Follows file naming conventions
- [ ] Follows coding style (indentation, comments, naming)
- [ ] Adheres to architecture rules
- [ ] Tested on hardware (if applicable)

---

## Project Structure

The Phoenix SDR codebase is organized as follows:

```
Phoenix/
└── code/
    ├── src/
    │   └── PhoenixSketch/        # Main source code
    │       ├── PhoenixSketch.ino # Arduino entry point
    │       ├── SDT.h             # Central definitions hub
    │       ├── Loop.cpp/h        # Main program loop
    │       ├── *Sm.cpp/h         # State machines (generated)
    │       ├── *Board.cpp/h      # Hardware modules
    │       ├── DSP*.cpp/h        # Signal processing
    │       ├── MainBoard_Display*.cpp/h  # Display system
    │       └── FrontPanel*.cpp/h # User interface
    └── test/                     # Unit tests and mocks
        ├── *_test.cpp           # Test files
        └── *_mock.cpp           # Mock implementations
```

### Directory Organization by Module Type

- **Core Infrastructure** (`SDT.h`, `Loop.cpp/h`, `PhoenixSketch.ino`): System foundations
- **State Machines** (`ModeSm.*`, `UISm.*`): Generated from UML diagrams
- **Hardware Modules** (`RFBoard.*`, `LPFBoard.*`, `BPFBoard.*`): Device control
- **Display System** (`MainBoard_Display*.cpp/h`): UI and graphics
- **DSP Modules** (`DSP*.cpp/h`): Signal processing
- **Front Panel** (`FrontPanel*.cpp/h`): Physical interface
- **Communication** (`CAT.cpp/h`): External control

## File Naming Conventions

Phoenix uses consistent naming patterns to show relationships between modules:

### Basic Patterns

1. **Paired Files**: Every module has both header and implementation files
   - `ModuleName.h` + `ModuleName.cpp`
   - Example: `RFBoard.h` + `RFBoard.cpp`

2. **Hierarchical Naming**: Use underscores to show module hierarchy
   - `ParentModule_Submodule.cpp`
   - Example: `MainBoard_Display.cpp` → `MainBoard_DisplayHome.cpp` → `MainBoard_DisplayMenus.cpp`

3. **Hardware-Specific Layers**: Separate device-specific code
   - `Module_DeviceSpecific.cpp`
   - Example: `RFBoard_si5351.cpp`, `LPFBoard_AD7991.cpp`

### Suffixes and Conventions

- **State Machines**: `*Sm.cpp/h` (e.g., `ModeSm`, `UISm`)
- **Hardware Modules**: `*Board` suffix (e.g., `RFBoard`, `LPFBoard`)
- **Test Files**: `*_test.cpp` (e.g., `RFBoard_test.cpp`)
- **Mock Files**: `*_mock.cpp` (e.g., `si5351_mock.cpp`)

### Examples

```
RFBoard.h              # RF board public interface
RFBoard.cpp            # RF board implementation
RFBoard_si5351.h       # Si5351 VFO control (hardware-specific)
RFBoard_si5351.cpp     # Si5351 implementation
RFBoard_test.cpp       # RF board unit tests

MainBoard_Display.h    # Master display definitions
MainBoard_Display.cpp  # Core display functions
MainBoard_DisplayHome.cpp   # Home screen rendering
MainBoard_DisplayMenus.cpp  # Menu system rendering
```

## Coding Style

### Header File Structure

All header files follow this pattern:

```cpp
#ifndef MODULENAME_H
#define MODULENAME_H
#include "SDT.h"

// Public function declarations
errno_t InitializeModule(void);
void SetModuleState(int32_t state);
int32_t GetModuleState(void);

// Test-only functions (if needed)
uint16_t GetModuleRegisterState(void); // For unit testing

#endif // MODULENAME_H
```

### Implementation File Structure

```cpp
#include "ModuleName.h"

///////////////////////////////////////////////////////////////////////////////
// Variables that are only visible from within this file
///////////////////////////////////////////////////////////////////////////////

static bool moduleInitialized = false;
static uint16_t moduleState = 0;
static Adafruit_MCP23X17 mcpDevice;

///////////////////////////////////////////////////////////////////////////////
// Functions that are only visible from within this file
///////////////////////////////////////////////////////////////////////////////

/**
 * PRIVATE: Initialize the I2C connection for this module.
 *
 * This function is called internally by public initialization functions
 * and should not be exposed in the public interface.
 *
 * @return true if initialization successful, false otherwise
 */
static bool InitI2C(void){
    if (!mcpDevice.begin_I2C(MODULE_I2C_ADDR)) {
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Public functions
///////////////////////////////////////////////////////////////////////////////

/**
 * Initialize the module and configure hardware.
 *
 * This function must be called before using any other module functions.
 * It initializes I2C communication and sets default hardware state.
 *
 * @return ESUCCESS on success, error code on failure
 */
errno_t InitializeModule(void){
    if (!moduleInitialized){
        moduleInitialized = InitI2C();
    }

    if (!moduleInitialized){
        return ENOI2C;
    }

    // Configure default state
    SetModuleState(0);

    return ESUCCESS;
}
```

### Commenting Guidelines

1. **Doxygen-Style Comments** for public functions:
   ```cpp
   /**
    * Brief description of what the function does.
    *
    * Detailed explanation if needed, including any important
    * behavior, side effects, or usage notes.
    *
    * @param paramName Description of parameter
    * @return Description of return value
    */
   ```

2. **Mark Private Functions**:
   ```cpp
   /**
    * PRIVATE: Brief description.
    *
    * PRIVATE: Longer explanation of why this is private and
    * how it's used internally.
    *
    * @param paramName Description
    * @return Description
    */
   static returnType PrivateFunction(paramType paramName){
       // implementation
   }
   ```

3. **Section Headers**:
   ```cpp
   ///////////////////////////////////////////////////////////////////////////////
   // SECTION NAME
   ///////////////////////////////////////////////////////////////////////////////
   ```

4. **Inline Comments**: Use `//` for single-line explanations
   ```cpp
   // Set all pins to zero. This means no attenuation
   SET_RF_GPA_RXATT(0x00);
   ```

### Naming Conventions

1. **Functions**: CamelCase starting with uppercase
   - `InitializeModule()`, `SetRXAttenuation()`, `GetTXRXFreq_dHz()`

2. **Variables**: camelCase starting with lowercase
   - `moduleInitialized`, `oldMultiple`, `TxRxFreq_old`

3. **Constants**: UPPERCASE_WITH_UNDERSCORES
   - `MAX_ATTENUATION_VAL_DBx2`, `SI5351_DRIVE_CURRENT`

4. **Static Variables**: Lowercase with `_old` for private module state
   - `static bool boardInitialized = false;`
   - `static uint8_t mcpA_old = 0x00;`

5. **Include Units in Names**: Make units explicit
   - `frequency_dHz` (decihertz = Hz × 10)
   - `Attenuation_dB` (decibels)
   - `ditDuration_ms` (milliseconds)

### Indentation and Formatting

- Use **4 spaces** for indentation (not tabs)
- Opening braces on same line for functions: `void Func(void){`
- Align related code vertically when it improves readability
- Keep lines under 100 characters when practical

## Architecture Rules

The Phoenix SDR follows strict architectural patterns. Understanding these rules is critical for successful contributions.

### 1. Central Hardware State Register

**ALL hardware state is stored in a single 32-bit register.** Add a second register if additional hardware state tracking is needed.

```cpp
// Defined in SDT.h
extern uint32_t hardwareRegister;
// Bits 0-3:   LPF band selection (BCD)
// Bits 4-5:   Antenna selection
// Bits 6-15:  Control flags (XVTR, PA, BPF, RXTX, CW, etc.)
// Bits 16-21: TX attenuator value
// Bits 22-27: RX attenuator value
// Bits 28-31: BPF band selection (BCD)
```

**RULE**: Use bit manipulation macros from `SDT.h` or define your own to access the register:

```cpp
#define GET_BIT(byte, bit) (((byte) >> (bit)) & 1)
#define SET_BIT(byte, bit) ((byte) |= (1 << (bit)))
#define CLEAR_BIT(byte, bit) ((byte) &= ~(1 << (bit)))
#define TOGGLE_BIT(byte, bit) ((byte) ^= (1 << (bit)))
```

**RULE**: Macros that change the hardware register must log this change in the rolling hardware buffer by including a call to `buffer_add()`.

**Example from RFBoard.cpp**:
```cpp
#define SET_RF_GPA_RXATT(val) (hardwareRegister = (hardwareRegister & 0xF03FFFFF) | (((uint32_t)val & 0x0000003F) << RXATTLSB));buffer_add()
```

### 2. State Machine Control

**ALL hardware state transitions must go through state machines.**

The project uses StateSmith-generated state machines for:

- **ModeSm**: Radio operating mode (SSB_RECEIVE, SSB_TRANSMIT, CW_TRANSMIT, etc.)
- **UISm**: User interface states
- **Tune**: Frequency control (TuneReceive, TuneSSBTX, TuneCWTX)

**RULE**: Do NOT directly change hardware state in response to user input. Instead:

1. Queue an event for the appropriate state machine
2. Let the state machine handle the transition
3. State machine entry/exit actions modify hardware

**Example**:
```cpp
// WRONG: Directly changing hardware
void HandlePTTPress(void){
    SelectTXMode();  // DON'T DO THIS
}

// RIGHT: Send event to state machine
void HandlePTTPress(void){
    ModeSm_dispatch_event(&ModeSm, ModeSm_EventId_PTT_PRESSED);
}
```

### 3. Hardware Module Encapsulation

**Hardware modules must have clean public interfaces and private implementations.**

Pattern:

- **Small header** (~50-100 lines): Clean public API only
- **Large implementation** (500+ lines): Static variables, hardware communication, caching
- **Hardware-specific layer** (optional): Device-specific code in separate file

**Example Structure** (RFBoard):

**RFBoard.h** (public interface):
```cpp
#ifndef RFBOARD_H
#define RFBOARD_H
#include "SDT.h"

// Clean public API
errno_t SetRXAttenuation(float32_t rxAttenuation_dB);
float32_t GetRXAttenuation(void);
void SetSSBVFOFrequency(int64_t frequency_dHz);
errno_t InitRFBoard(void);

#endif // RFBOARD_H
```

**RFBoard.cpp** (implementation with private state):
```cpp
#include "SDT.h"
#include "RFBoard_si5351.h"

///////////////////////////////////////////////////////////////////////////////
// Variables that are only visible from within this file
///////////////////////////////////////////////////////////////////////////////

static Adafruit_MCP23X17 mcpAtten;
static bool boardInitialized = false;
static uint8_t mcpA_old = 0x00;
static uint8_t mcpB_old = 0x00;

///////////////////////////////////////////////////////////////////////////////
// Functions that are only visible from within this file
///////////////////////////////////////////////////////////////////////////////

static bool InitI2C(void){
    // Private implementation
}

static bool WriteGPIOARegister(void){
    // Private implementation
}

///////////////////////////////////////////////////////////////////////////////
// Public functions
///////////////////////////////////////////////////////////////////////////////

errno_t SetRXAttenuation(float32_t rxAttenuation_dB){
    // Public implementation
}
```

**RFBoard_si5351.cpp** (hardware-specific layer):
```cpp
// Si5351 VFO control - device-specific code
```

### 4. Display System Architecture

**Display functions are READ-ONLY.**

Display code follows a strict pattern:

- Read state from global variables
- Render based on that state
- DO NOT modify external state variables (with the exception of the UPDATE and CALIBRATE display states)

**Example from MainBoard_DisplayHome.cpp**:
```cpp
/**
 * Functions in this file follow a strict read-only pattern:
 * - Read the state of the radio from global variables
 * - Draw on the display based on that state
 * - DO NOT modify any external state variables
 */
void DrawVFOPanes(void) {
    int64_t TxRxFreq = GetTXRXFreq_dHz()/100;  // READ state

    // Check if update needed
    if ((TxRxFreq == TxRxFreq_old) && (!PaneVFOA.stale))
        return;

    // DRAW based on state
    tft.setFont(&FreeSansBold24pt7b);
    tft.print(freqBuffer);

    // DO NOT modify radio state here
}
```

### 5. Pane-Based Display System

The display is divided into independent panes, each with:

- Position and size
- Draw function pointer
- Stale flag for selective updates

**Structure**:
```cpp
struct Pane {
    uint16_t x0;
    uint16_t y0;
    uint16_t width;
    uint16_t height;
    void (*DrawFunction)(void);  // Function to render this pane
    bool stale;                  // Flag indicating redraw needed
};
```

**Usage Pattern**:
```cpp
// Mark pane as stale when underlying data changes
PaneVFOA.stale = true;

// Main display loop checks stale flags and redraws
void DrawDisplay(void){
    for (int i=0; i<NUMBER_OF_PANES; i++){
        if (WindowPanes[i]->stale){
            WindowPanes[i]->DrawFunction();
            WindowPanes[i]->stale = false;
        }
    }
}
```

### 6. Register Caching Pattern

**Always cache register values to avoid unnecessary I2C writes.**

This pattern minimizes I2C traffic and improves real-time performance:

```cpp
static uint8_t mcpA_old = 0x00;

static bool WriteGPIOARegister(void){
    if (RF_GPA_RXATT_STATE == mcpA_old) return false;  // No change

    mcpAtten.writeGPIOA(RF_GPA_RXATT_STATE);  // Only write if changed
    mcpA_old = RF_GPA_RXATT_STATE;  // Update cache
    return true;
}
```

### 7. Error Handling

Use `errno_t` return types for functions that can fail:

```cpp
errno_t InitializeModule(void){
    if (!hardware_present){
        return ENOI2C;
    }

    if (!configuration_valid){
        return EINVAL;
    }

    return ESUCCESS;
}
```

Common error codes (defined in SDT.h or by system):

- `ESUCCESS`: Operation successful
- `ENOI2C`: I2C communication failed
- `EINVAL`: Invalid parameter
- `EGPIOWRITEFAIL`: GPIO write failed

## Adding Display Screens

Adding new display functionality requires understanding the pane-based display system.

### Display System Overview

The display is divided into independent panes. For instance, on the HOME screen these panes are:

- `PaneVFOA`, `PaneVFOB`: VFO frequency displays
- `PaneSpectrum`: FFT spectrum display
- `PaneSWR`: SWR meter
- etc. (12 panes total)

Each pane has a draw function that is called when its `stale` flag is set.

### Step 1: Define Pane Structure

In `MainBoard_DisplayHome.cpp`, add a new pane:

```cpp
Pane PaneNewFeature = {
    .x0 = 10,           // X position
    .y0 = 400,          // Y position
    .width = 200,       // Width in pixels
    .height = 60,       // Height in pixels
    .DrawFunction = DrawNewFeature,  // Your draw function
    .stale = true       // Initially needs drawing
};
```

Add to the pane array:
```cpp
Pane* WindowPanes[NUMBER_OF_PANES] = {
    &PaneVFOA,
    &PaneVFOB,
    // ... other panes ...
    &PaneNewFeature,  // Add here
};
```

Don't forget to increment `NUMBER_OF_PANES`:
```cpp
#define NUMBER_OF_PANES 13  // Was 12, now 13
```

### Step 2: Create Draw Function

Add to `MainBoard_DisplayHome.cpp` (or create a new file following the naming convention):

```cpp
/**
 * Draw the new feature pane.
 *
 * This function follows the read-only pattern:
 * - Read state from global variables
 * - Draw based on that state
 * - DO NOT modify state
 */
void DrawNewFeature(void) {
    // Static variables to track previous state
    static int32_t oldValue = -1;

    // Read current state
    int32_t currentValue = GetNewFeatureValue();

    // Check if update needed
    if ((currentValue == oldValue) && (!PaneNewFeature.stale))
        return;

    oldValue = currentValue;

    // Erase old content
    Rectangle rect;
    rect.x0 = PaneNewFeature.x0;
    rect.y0 = PaneNewFeature.y0;
    rect.width = PaneNewFeature.width;
    rect.height = PaneNewFeature.height;
    BlankBox(&rect);

    // Draw new content
    tft.setCursor(PaneNewFeature.x0, PaneNewFeature.y0);
    tft.setFont(&FreeSansBold18pt7b);
    tft.setTextColor(RA8875_WHITE);
    tft.print("Value: ");
    tft.print(currentValue);

    // Clear stale flag
    PaneNewFeature.stale = false;
}
```


## Testing Guidelines

All new code must include comprehensive unit tests using Google Test.

### Test File Structure

```cpp
#include "gtest/gtest.h"
#include "../src/PhoenixSketch/SDT.h"
#include "../src/PhoenixSketch/YourModule.h"

// Use TEST() for independent tests
TEST(ModuleName, TestName) {
    // Arrange
    InitializeModule();

    // Act
    errno_t rv = SetModuleState(5);

    // Assert
    EXPECT_EQ(rv, ESUCCESS);
    EXPECT_EQ(GetModuleState(), 5);
}

// Use TEST_F() for tests requiring setup/teardown
class ModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset state before each test
        InitializeModule();
    }

    void TearDown() override {
        // Clean up after test
    }
};

TEST_F(ModuleTest, ComplexBehavior) {
    // Test using fixture
}
```

### Test Coverage Requirements

Your tests should cover:

1. **Initialization**: Module starts correctly
2. **Valid Inputs**: All valid parameter values work
3. **Boundary Conditions**: Min/max values handled correctly
4. **Invalid Inputs**: Out-of-range values are clamped or rejected
5. **State Transitions**: All state changes work correctly
6. **Error Conditions**: Failures are handled gracefully

### Running Tests

```bash
# Build and run all tests
cd code/test/build
cmake ../ && make && ctest --output-on-failure

# Run specific test suite
ctest -R NewModule --output-on-failure

# Run with verbose output
ctest -V

# Run individual test binary directly
./all_NewModule_tests
```

### Mock Objects

When testing modules that depend on hardware, use the provided mock implementations in `code/test/*_mock.cpp`:

- `Arduino_mock.cpp`: Arduino framework functions
- `Adafruit_I2CDevice_mock.cpp`: I2C device communication
- `RA8875_mock.cpp`: Display driver
- `si5351_mock.cpp`: VFO clock generator
- `OpenAudio_ArduinoLibrary_mock.cpp`: Audio DSP

Mocks simulate hardware behavior without requiring actual hardware, enabling fast, reliable unit tests.

## State Machine Modifications

State machines are generated from UML diagrams using StateSmith. **Do not edit the generated .cpp/.h files directly.**

### Modifying State Machines

1. **Open the .drawio file** in draw.io (e.g., `ModeSm.drawio`)
2. **Edit the state machine diagram**
3. **Regenerate the code** using StateSmith:
   ```bash
   ss.cli run -h
   ```
4. **Test the changes** thoroughly

### State Machine Files

- `ModeSm.drawio` → `ModeSm.cpp` / `ModeSm.h`
- `UISm.drawio` → `UISm.cpp` / `UISm.h`

**NEVER** manually edit the generated `.cpp` or `.h` files - your changes will be overwritten on the next generation.

### State Machine Event Pattern

```cpp
// Define events in the .drawio diagram
// Events are generated as enum values

// Dispatch events from your code
ModeSm_dispatch_event(&ModeSm, ModeSm_EventId_PTT_PRESSED);

// State machine handles transitions and calls entry/exit actions
```

## Pull Request Process

### Before Submitting

1. **Run all unit tests** and ensure they pass:
   ```bash
   cd code/test/build
   cmake ../ && make && ctest --output-on-failure
   ```

2. **Test on actual hardware** if modifying hardware control code

3. **Add unit tests** for all new functionality

4. **Follow coding style** guidelines in this document

5. **Update documentation** if adding new features

### Submitting a Pull Request

Fork the main GitHub repository into your own account.

1. **Create a feature branch**:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes** following this guide

3. **Commit with clear messages**:
   ```bash
   git commit -m "Add power meter display pane

   - Created PanePowerMeter with forward power display
   - Added DrawPowerMeter() function
   - Integrated with main display loop
   - Added unit tests for power reading"
   ```

4. **Push your branch**:
   ```bash
   git push origin feature/your-feature-name
   ```

5. **Open a pull request** with:
   - Clear description of changes
   - Motivation for the change
   - Testing performed

### PR Review Checklist

Your PR will be reviewed for:

- [ ] Follows file naming conventions
- [ ] Follows coding style (indentation, comments, naming)
- [ ] Adheres to architecture rules (state machines, hardware register, etc.)
- [ ] Includes comprehensive unit tests
- [ ] All tests pass

## Questions?

If you have questions about contributing:

1. Check the main [README.md](README.md)
2. Review existing code for examples
3. Open an issue for discussion

Thank you for contributing to Phoenix SDR!
