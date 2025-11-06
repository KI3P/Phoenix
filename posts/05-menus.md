# Menu System: Building a Hierarchical Configuration Interface

## Introduction

The Phoenix SDR needs a way to configure dozens of parameters: RF power levels, CW keying speed, filter bandwidths, antenna selection, and display settings. These parameters have different types (integers, floats, booleans, enums), different valid ranges, and sometimes require immediate hardware updates when changed. Managing this complexity while maintaining type safety and providing a good user experience is the challenge addressed by the menu system.

This post explores how the Phoenix firmware implements a hierarchical, struct-based menu system that provides:

1. **Type-safe parameter modification** through a generic variable abstraction
2. **Two types of menu actions**: variable adjustment and function execution
3. **Real-time parameter updates** using the UPDATE state in the UI state machine
4. **Immediate hardware response** through post-update callback functions

The menu system is built entirely from C structs and function pointers, creating a data-driven architecture where adding new configuration options requires no changes to the menu navigation code itself.

## Menu System Architecture

### The Three-Struct Hierarchy

The menu system uses three interconnected struct types that form a hierarchy from top-level categories down to individual configurable items:

```
PrimaryMenuOption (Categories)
    ↓
SecondaryMenuOption (Individual Items)
    ↓
VariableParameter (Type-Safe Variable Metadata)
```

At the top level, users navigate between primary menu categories like "RF Options" or "CW Options". Selecting a category reveals secondary menu items — the actual configurable parameters. Some of these items allow variable adjustment (entering the UPDATE state), while others execute functions immediately.

### Data Structure Definitions

Let's examine each struct in detail, understanding the design decisions behind each field.

## VariableParameter: Type-Safe Parameter Abstraction

The heart of the menu system is the `VariableParameter` struct (`MainBoard_Display.h:17-29`), which provides a generic way to describe any configurable variable regardless of its underlying type:

```cpp
enum VarType {
    TYPE_I8,
    TYPE_I16,
    TYPE_I32,
    TYPE_I64,
    TYPE_F32,
    TYPE_KeyTypeId,
    TYPE_BOOL
};

struct VariableParameter {
    void *variable;      // Pointer to the variable
    VarType type;        // Type of the variable
    union {
        struct { int8_t min; int8_t max; int8_t step;} i8;
        struct { int16_t min; int16_t max; int16_t step;} i16;
        struct { int32_t min; int32_t max; int32_t step;} i32;
        struct { int64_t min; int64_t max; int64_t step;} i64;
        struct { float32_t min; float32_t max; float32_t step;} f32;
        struct { KeyTypeId min; KeyTypeId max; int8_t step;} keyType;
        struct { bool min; bool max; int8_t step;} b;
    } limits;
};
```

This design solves a fundamental problem: how do you write generic increment/decrement functions that work with variables of different types while maintaining bounds checking?

The solution uses three components:

1. **Void Pointer**: The `variable` field stores a generic pointer to the actual variable being configured. This could point to an `int8_t` RF power level, a `float32_t` audio gain, or a `bool` toggle switch.

2. **Type Tag**: The `type` enum tells increment/decrement functions how to interpret the void pointer. When you cast `variable` back to its concrete type, you need to know which type to cast to.

3. **Type-Specific Limits**: The `limits` union provides min/max/step values in the same type as the variable itself. Using a union keeps the struct size small (only the largest member's size is allocated) while allowing type-safe access to appropriate limits.

For example, an SSB power parameter might be defined as:

```cpp
VariableParameter ssbPower = {
    .variable = &ssbPowerLevel,  // Points to global int8_t
    .type = TYPE_I8,
    .limits.i8 = { .min = 0, .max = 100, .step = 5 }
};
```

This metadata fully describes how to safely modify `ssbPowerLevel`: it's an 8-bit integer, ranges from 0 to 100 watts, and should change in 5-watt increments when the user rotates the encoder.

### Dynamic Variable Pointers: Per-Band Parameters

Some radio parameters need different values for each amateur radio band. For example, the optimal SSB transmit power for the 20-meter band might be different from the 40-meter band due to different antenna characteristics and propagation conditions. Rather than creating separate menu entries for each band, the Phoenix firmware uses **dynamic variable pointers** that automatically point to the correct array element based on the current band.

Consider the actual SSB power parameter definition (`MainBoard_Display.cpp:2293-2297`):

```cpp
VariableParameter ssbPower = {
    .variable = NULL,  // Will be set dynamically to &ED.powerOutSSB[ED.currentBand[ED.activeVFO]]
    .type = TYPE_F32,
    .limits = {.f32 = {.min = 0.0f, .max=20.0f, .step=0.5f}}
};
```

Notice that `.variable = NULL` — the pointer isn't initialized to a specific variable. Instead, it's updated dynamically by the `UpdateArrayVariables()` function (`MainBoard_Display.cpp:2475-2492`):

```cpp
void UpdateArrayVariables(void){
    // Update array-based variable pointers to point to current band element

    // RF Set Menu
    ssbPower.variable = &ED.powerOutSSB[ED.currentBand[ED.activeVFO]];
    cwPower.variable = &ED.powerOutCW[ED.currentBand[ED.activeVFO]];
    rxAtten.variable = &ED.RAtten[ED.currentBand[ED.activeVFO]];
    txAttenCW.variable = &ED.XAttenCW[ED.currentBand[ED.activeVFO]];
    txAttenSSB.variable = &ED.XAttenSSB[ED.currentBand[ED.activeVFO]];
    antenna.variable = &ED.antennaSelection[ED.currentBand[ED.activeVFO]];

    // Display menu
    spectrumfloor.variable = &ED.spectrumNoiseFloor[ED.currentBand[ED.activeVFO]];
}
```

This function is called whenever:

1. **VFO changes**: Switching from VFO A to VFO B (which might be on different bands)
2. **Band changes**: Tuning to a different frequency that crosses a band boundary
3. **Entering menus**: To ensure the menu shows the correct band's values

The underlying data structure (`SDT.h:237-261`) stores these as arrays indexed by band:

```cpp
struct EEPROMData {
    // Per-band settings (12 bands)
    float32_t powerOutSSB[NUMBER_OF_BANDS];
    float32_t powerOutCW[NUMBER_OF_BANDS];
    float32_t RAtten[NUMBER_OF_BANDS];
    int16_t spectrumNoiseFloor[NUMBER_OF_BANDS];
    // ... etc
};
```

**Why this design?** The alternative would be a massive menu with entries like "SSB Power 20m", "SSB Power 40m", "SSB Power 80m", etc. — duplicating menu items for each band. The dynamic pointer approach provides a clean user experience: the menu shows "SSB Power" and automatically modifies the correct band's value based on the current operating frequency.

**When UpdateArrayVariables() is called:**

The function is invoked in several key places:

1. **DrawMainMenu()** and **DrawParameter()** (`MainBoard_Display.cpp:2714`, `2748`): Check if VFO or band changed, and if so, update pointers and mark display for redraw
2. **Initialization**: During menu system startup to set initial pointers

This ensures the menu system always operates on the correct band's data, providing a seamless user experience where parameter changes automatically apply to the current band.

## SecondaryMenuOption: The Configurable Items

The `SecondaryMenuOption` struct (`MainBoard_Display.h:41-47`) represents individual menu items that users can select and configure:

```cpp
enum optionType {
    variableOption,
    functionOption
};

struct SecondaryMenuOption {
    const char *label;
    optionType action;
    VariableParameter *varPam;
    void *func;
    void *postUpdateFunc;
};
```

The key design insight here is that menu items can do two fundamentally different things:

### Variable Options
These allow the user to increment or decrement a value. When selected, the UI enters the UPDATE state, displays the current value, and lets the user adjust it in real-time with encoder rotation.

Example: "SSB Power" is a variable option. Selecting it shows the current power level and allows adjustment.

### Function Options
These execute a function immediately when selected, then return to the HOME screen. No value to adjust — just an action to perform.

Example: "Flip Paddle" is a function option that swaps the dit/dah paddles for left-handed operation.

The struct uses **selective field usage** based on the `action` type:

- **variableOption**: Uses `label`, `varPam`, and optionally `postUpdateFunc`
- **functionOption**: Uses `label` and `func`

The `postUpdateFunc` field deserves special attention. Some parameters affect hardware state and need immediate application. For example, changing the antenna selection should immediately execute the code to change the active antenna. The `postUpdateFunc` callback provides this hook.

## PrimaryMenuOption: The Top-Level Categories

The `PrimaryMenuOption` struct (`MainBoard_Display.h:50-54`) groups related secondary menu items into categories:

```cpp
struct PrimaryMenuOption {
    const char *label;
    SecondaryMenuOption *secondary;
    size_t length;
};
```

This is the simplest struct — it just associates a category label like "RF Options" with an array of secondary menu items and the array length.

## Building Menus: Real Examples from the Code

Now let's see how these structs are used to construct actual menus.

### RF Options Menu

The RF Options menu (`MainBoard_Display.cpp:2347-2355`) configures RF hardware parameters:

```cpp
struct SecondaryMenuOption RFSet[7] = {
    "SSB Power", variableOption, &ssbPower, NULL, NULL,
    "CW Power", variableOption, &cwPower, NULL, NULL,
    "Gain", variableOption, &gain, NULL, NULL,
    "RX Attenuation", variableOption, &rxAtten, NULL, (void *)UpdateRatten,
    "TX Attenuation (CW)", variableOption, &txAttenCW, NULL, (void *)UpdateTXAttenCW,
    "TX Attenuation (SSB)", variableOption, &txAttenSSB, NULL, (void *)UpdateTXAttenSSB,
    "Antenna", variableOption, &antenna, NULL, (void *)UpdateTuneState,
};
```

Each line declares a configurable parameter. Notice that the first three items have `NULL` for `postUpdateFunc` — these parameters are applied at specific points in the DSP chain or mode transitions, not immediately when changed.

But the attenuation and antenna parameters have callbacks:

- `UpdateRatten()`, `UpdateTXAttenCW()`, `UpdateTXAttenSSB()`: These update the RF board hardware registers immediately, changing the attenuator settings in real-time.
- `UpdateTuneState()`: Changing the antenna triggers this call, which updates the antenna selection in hardware (among other changes).

### CW Options Menu

The CW Options menu (`MainBoard_Display.cpp:2393-2400`) demonstrates the mix of variable and function options:

```cpp
struct SecondaryMenuOption CWOptions[6] = {
    "WPM", variableOption, &wpm, NULL, (void *)UpdateDitLength,
    "Straight key", functionOption, NULL, (void *)SelectStraightKey, NULL,
    "Keyer", functionOption, NULL, (void *)SelectKeyer, NULL,
    "Flip paddle", functionOption, NULL, (void *)FlipPaddle, NULL,
    "CW Filter", variableOption, &cwf, NULL, NULL,
    "Sidetone volume", variableOption, &stv, NULL, NULL,
};
```

The first line, "WPM" (words per minute), is a variable option. When changed, it calls `UpdateDitLength()` to recalculate the timing for dit and dah durations.

The next three items — "Straight key", "Keyer", and "Flip paddle" — are function options. They execute immediately:

- `SelectStraightKey()`: Switches to straight key mode (manual keying, no iambic logic)
- `SelectKeyer()`: Enables the iambic keyer state machine
- `FlipPaddle()`: Swaps left/right paddle assignments

The remaining items are variable options for filter bandwidth and sidetone volume.

### Assembling the Primary Menu

Finally, the primary menu array (`MainBoard_Display.cpp:2447-2452`) links everything together:

```cpp
struct PrimaryMenuOption primaryMenu[4] = {
    "RF Options", RFSet, sizeof(RFSet)/sizeof(RFSet[0]),
    "CW Options", CWOptions, sizeof(CWOptions)/sizeof(CWOptions[0]),
    "Calibration", CalOptions, sizeof(CalOptions)/sizeof(CalOptions[0]),
    "Display", DisplayOptions, sizeof(DisplayOptions)/sizeof(DisplayOptions[0]),
};
```

Each entry associates a category label with its secondary menu array. The length calculation `sizeof(RFSet)/sizeof(RFSet[0])` automatically determines the number of items in each array, so adding new menu items doesn't require manual count updates.

## The Two Types of Menu Actions

Understanding the distinction between variable and function options is crucial to understanding the menu flow.

### Variable Options: Enter UPDATE State

When the user selects a variable option from the secondary menu, the UI state machine transitions to the UPDATE state. In this state:

1. The HOME screen remains visible in the background
2. A red-bordered parameter badge overlays the display, showing the current value
3. Encoder rotation increments or decrements the value in real-time
4. The value is constrained to its min/max limits
5. If a `postUpdateFunc` is defined, it's called after each adjustment
6. Pressing SELECT or HOME confirms the change and returns to the HOME screen

This provides immediate visual feedback — you see the parameter name, current value, and any effects of the change (e.g., watching S-meter readings as you adjust RX attenuation).

### Function Options: Execute and Return

When the user selects a function option, the menu system:

1. Immediately calls the associated function
2. Returns directly to the HOME screen
3. Never enters the UPDATE state

This makes sense for actions that don't have a configurable value. "Flip paddle" doesn't need real-time adjustment — it's a one-shot toggle.

The function option handling is implemented in the `DrawDisplay()` function (`MainBoard_Display.cpp:2930-2938`):

```cpp
case (UISm_StateId_UPDATE):{
    if (primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].action == variableOption){
        // Variable option: show parameter adjustment interface
        if (uiSM.vars.clearScreen)
            redrawParameter = true;
        DrawHome();
        DrawParameter();
    } else {
        // Function option: execute and return home
        UISm_dispatch_event(&uiSM, UISm_EventId_HOME);
        void (*funcPtr)(void) = (void (*)(void))primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].func;
        if (funcPtr != NULL) {
            funcPtr();
        }
    }
    break;
}
```

When rendering the UPDATE state, the code first checks the `action` type. If it's a function option, it dispatches a HOME event to the state machine (transitioning back to the home screen) and then casts the void pointer to a function pointer and calls it.

## The UPDATE State: Real-Time Parameter Adjustment

The UPDATE state is where the magic of real-time parameter adjustment happens. Understanding this state requires examining the UI state machine and how it integrates with the main loop's event handling.

### UI State Machine Context

The Phoenix firmware uses a StateSmith-generated state machine (`UISm`) to manage the user interface. The state machine has seven states (`UISm.h:29-38`):

- **SPLASH**: Boot-up logo screen
- **HOME**: Main operating display (spectrum, VFO, S-meter)
- **MAIN_MENU**: Top-level menu category selection
- **SECONDARY_MENU**: Individual parameter selection within a category
- **UPDATE**: Real-time parameter value adjustment
- **CALIBRATION**: Special calibration mode

State transitions are driven by button events:

- **MENU**: Navigate menu hierarchy
- **SELECT**: Confirm selection or enter UPDATE state
- **HOME**: Return to main display
- **CAL**: Enter calibration mode

### Entering the UPDATE State

The transition to UPDATE state happens when the user presses SELECT while in the SECONDARY_MENU state, but only for variable options. The state transition code (`UISm.cpp:403-417`) looks like this:

```cpp
static void SECONDARY_MENU_select(UISm* sm)
{
    // SECONDARY_MENU behavior
    // uml: SELECT TransitionTo(UPDATE)
    {
        // Step 1: Exit states until we reach `ROOT` state
        SECONDARY_MENU_exit(sm);

        // Step 2: Transition action: (empty)

        // Step 3: Enter/move towards transition target `UPDATE`
        UPDATE_enter(sm);

        // Step 4: complete transition. Ends event dispatch.
        return;
    }
}
```

This is generated code from the StateSmith UML diagram, following a consistent pattern: exit the current state, perform any transition actions, enter the new state, and complete.

### UPDATE State Entry Behavior

When entering the UPDATE state (`UISm.cpp:475-485`), the state machine sets a flag to trigger display initialization:

```cpp
static void UPDATE_enter(UISm* sm)
{
    sm->state_id = UISm_StateId_UPDATE;

    // UPDATE behavior
    // uml: enter / { clearScreen = true; }
    {
        // Step 1: execute action `clearScreen = true;`
        sm->vars.clearScreen = true;
    }
}
```

The `clearScreen` flag tells the rendering code that this is the first frame of the UPDATE state, so it should redraw the parameter badge. On subsequent frames, the badge is only redrawn when the value actually changes, minimizing screen flicker.

### Main Loop Event Handling

While in the UPDATE state, the main loop (`Loop.cpp:689-716`) routes encoder rotation events to the appropriate increment/decrement functions:

```cpp
case (iFILTER_INCREASE):{
    switch (uiSM.state_id){
        // ... other states ...
        case (UISm_StateId_UPDATE):{
            IncrementValue();
            break;
        }
        default:
            break;
    }
    break;
}
case (iFILTER_DECREASE):{
    switch (uiSM.state_id){
        // ... other states ...
        case (UISm_StateId_UPDATE):{
            DecrementValue();
            break;
        }
        default:
            break;
    }
    break;
}
```

The `iFILTER_INCREASE` and `iFILTER_DECREASE` events are generated by the encoder interrupt handler and placed in an event queue. The main loop processes this queue and dispatches to state-specific handlers.

Note the naming: "FILTER" events control the spectrum filter bandwidth in HOME state, but in UPDATE state they control parameter values. This is context-dependent event handling — the same physical encoder does different things depending on the current UI state.

### Rendering the UPDATE State

The display rendering for UPDATE state (`MainBoard_Display.cpp:2924-2939`) carefully distinguishes between variable and function options:

```cpp
case (UISm_StateId_UPDATE):{
    if (primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].action == variableOption){
        if (uiSM.vars.clearScreen)
            redrawParameter = true; // on first entry, draw the parameter
        DrawHome();
        DrawParameter();
    } else {
        // Function option: send us back to the HOME screen
        UISm_dispatch_event(&uiSM, UISm_EventId_HOME);
        // Cast void* to function pointer and call it
        void (*funcPtr)(void) = (void (*)(void))primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].func;
        if (funcPtr != NULL) {
            funcPtr();
        }
    }
    break;
}
```

For variable options:
1. Check if `clearScreen` flag is set (first frame)
2. Draw the HOME screen as the background
3. Draw the parameter badge overlay on top

For function options:
1. Dispatch a HOME event to exit UPDATE state
2. Cast the `func` void pointer to a function pointer
3. Call the function

The parameter badge itself is drawn by `DrawParameter()` (`MainBoard_Display.cpp:2743-2765`):

```cpp
void DrawParameter(void){
    // Update array variables if active VFO or band changed
    if ((oavfo != ED.activeVFO) || (oband != ED.currentBand[ED.activeVFO])){
        oavfo = ED.activeVFO;
        oband = ED.currentBand[ED.activeVFO];
        UpdateArrayVariables();
        redrawParameter = true;
    }

    if (redrawParameter){
        // Clear the badge area
        tft.fillRect(PaneNameBadge.x0, PaneNameBadge.y0,
                     PaneNameBadge.width, PaneNameBadge.height, RA8875_BLACK);

        // Draw red border
        tft.drawRect(PaneNameBadge.x0, PaneNameBadge.y0,
                     PaneNameBadge.width, PaneNameBadge.height, RA8875_RED);

        // Draw label and current value
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)0.5);
        tft.setCursor(PaneNameBadge.x0+5, PaneNameBadge.y0+5);
        tft.setTextColor(RA8875_WHITE);
        tft.print(primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].label);
        tft.print(": ");
        tft.print(GetVariableValueAsString(primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].varPam));
        redrawParameter = false;
    }
}
```

The function checks if the active VFO or band has changed — this matters for parameters stored in per-VFO, per-band arrays (like spectrum floor level). If changed, it updates the displayed variable pointers.

The display shows: `<label>: <value>` in white text with a distinctive red border, overlaying the home screen. This provides clear visual feedback that you're in parameter adjustment mode.

### Exiting the UPDATE State

The UPDATE state can be exited in three ways (`UISm.cpp:492-547`):

**1. HOME Button**: Return to HOME screen
```cpp
static void UPDATE_home(UISm* sm)
{
    // Exit UPDATE state
    UPDATE_exit(sm);

    // Enter HOME state
    HOME_enter(sm);

    return;
}
```

**2. SELECT Button**: Confirm and return to HOME
```cpp
static void UPDATE_select(UISm* sm)
{
    // Same as HOME button - confirms and returns
    UPDATE_exit(sm);
    HOME_enter(sm);
    return;
}
```

**3. CAL Button**: Jump to calibration mode
```cpp
static void UPDATE_cal(UISm* sm)
{
    // Exit UPDATE state
    UPDATE_exit(sm);

    // Enter CALIBRATION state
    CALIBRATION_enter(sm);

    return;
}
```

In all cases, the state machine follows the same pattern: exit current state, then enter target state. The UPDATE_exit function simply resets the state ID:

```cpp
static void UPDATE_exit(UISm* sm)
{
    sm->state_id = UISm_StateId_ROOT;
}
```

## Variable Manipulation: Type-Safe Increment and Decrement

The real work of modifying variables happens in the `IncrementVariable()` and `DecrementVariable()` functions. These functions must handle multiple data types while maintaining bounds checking.

### IncrementVariable Implementation

The increment function (`MainBoard_Display.cpp:2069-2139`) uses a switch statement on the `VarType` enum to handle each type appropriately:

```cpp
void IncrementVariable(const VariableParameter *bv) {
    if (bv->variable == NULL) {
        return;
    }

    switch (bv->type) {
        case TYPE_I8: {
            int8_t value = *(int8_t *)bv->variable;
            value = value + bv->limits.i8.step;
            if (value > bv->limits.i8.max){
                value = bv->limits.i8.max;
            }
            *(int8_t *)bv->variable = value;
            return;
        }
        case TYPE_I16: {
            int16_t value = *(int16_t *)bv->variable;
            value = value + bv->limits.i16.step;
            if (value > bv->limits.i16.max){
                value = bv->limits.i16.max;
            }
            *(int16_t *)bv->variable = value;
            return;
        }
        // ... similar cases for I32, I64, F32, KeyTypeId ...
        case TYPE_BOOL: {
            bool value = *(bool *)bv->variable;
            value = !value;  // Toggle
            *(bool *)bv->variable = value;
            return;
        }
    }
}
```

Each case follows the same pattern:

1. Cast the void pointer to the appropriate type
2. Dereference to read the current value
3. Add the step value
4. Clamp to the maximum limit
5. Write the new value back through the pointer

Boolean types are special-cased to toggle between true and false, since increment/decrement doesn't make sense for booleans.

This approach provides **type safety at runtime** — the type tag ensures we always cast to the correct type, and the limits union ensures we compare against limits of the matching type. While C doesn't have compile-time generics, this pattern achieves similar goals.

### DecrementVariable Implementation

The decrement function (`MainBoard_Display.cpp:2191-2261`) is nearly identical, but subtracts the step and clamps to the minimum:

```cpp
void DecrementVariable(const VariableParameter *bv) {
    if (bv->variable == NULL) {
        return;
    }
    switch (bv->type) {
        case TYPE_I8: {
            int8_t value = *(int8_t *)bv->variable;
            value = value - bv->limits.i8.step;
            if (value < bv->limits.i8.min){
                value = bv->limits.i8.min;
            }
            *(int8_t *)bv->variable = value;
            return;
        }
        // ... similar for other types ...
    }
}
```

The clamping behavior ensures the variable never exceeds its defined range, even if the user keeps rotating the encoder. This is critical for parameters that affect hardware — exceeding safe power levels or attenuation values could damage equipment or produce invalid RF output.

### Wrapper Functions: IncrementValue and DecrementValue

The main loop doesn't call `IncrementVariable()` and `DecrementVariable()` directly. Instead, it calls wrapper functions (`MainBoard_Display.cpp:2785-2819`) that add display updates and callback execution:

```cpp
void IncrementValue(void){
    IncrementVariable(primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].varPam);
    redrawParameter = true;
    void (*funcPtr)(void) = (void (*)(void))primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].postUpdateFunc;
    if (funcPtr != NULL) {
        funcPtr();
    }
}

void DecrementValue(void){
    DecrementVariable(primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].varPam);
    redrawParameter = true;
    void (*funcPtr)(void) = (void (*)(void))primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].postUpdateFunc;
    if (funcPtr != NULL) {
        funcPtr();
    }
}
```

These wrappers:

1. Call the appropriate increment/decrement function
2. Set `redrawParameter = true` to trigger display update on the next frame
3. Check if a post-update callback is defined
4. If so, cast the void pointer to a function pointer and call it

The `redrawParameter` flag is critical for efficiency. Rather than redrawing the badge every frame (which would waste CPU time and cause flicker), the code only redraws when the value actually changes.

## Post-Update Callbacks: Bridging UI to Hardware

Some parameter changes need to take effect immediately, not just be stored for later use. The `postUpdateFunc` callback mechanism provides this capability, allowing menu parameters to trigger hardware updates, DSP reconfiguration, or state machine transitions.

### Why Callbacks Are Necessary

Consider what happens when you change the RX attenuation from 0 dB to 10 dB:

**Without a callback**: The `rxAtten` variable is updated to 10, but nothing else happens. The RF board hardware still has 0 dB attenuation configured. The change won't take effect until the next mode transition (RX to TX and back), which could be minutes away.

**With a callback**: The `rxAtten` variable is updated to 10, and then `UpdateRatten()` is immediately called. This function writes the new attenuation value to the RF board's I2C GPIO registers, and the hardware attenuation changes in real-time. You can watch the S-meter reading drop as you increase attenuation — instant feedback.

### Callback Examples from the Codebase

Let's examine several real callback functions to understand what they do:

#### UpdateRatten(): Immediate Attenuator Update

When you adjust RX attenuation in the menu, `UpdateRatten()` updates the hardware immediately:

```cpp
void UpdateRatten(void){
    UpdateRFHardwareState();
}
```

This calls `UpdateRFHardwareState()`, which examines the current operating mode and reconfigures the RF board accordingly. The RF board API uses the write-on-change optimization discussed in the hardware states post, so if the attenuation hasn't actually changed, no I2C transaction occurs.

#### UpdateDitLength(): CW Timing Recalculation

When you change the CW speed (WPM), the dit and dah lengths must be recalculated immediately:

```cpp
void UpdateDitLength(void){
    // Recalculate dit length based on wpm
    // Formula: 1200 ms per dit at 1 WPM
    ditLength = 1200 / wpm;
    dahLength = 3 * ditLength;
    elementSpace = ditLength;
    characterSpace = 3 * ditLength;
}
```

Without this callback, changing WPM would require restarting the radio or entering/exiting CW mode for the change to take effect. With the callback, the new timing is active immediately.

### Callback Execution Flow

The callback execution happens in `IncrementValue()` and `DecrementValue()` after the variable has been modified:

```cpp
void IncrementValue(void){
    // 1. Modify the variable
    IncrementVariable(primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].varPam);

    // 2. Mark display for redraw
    redrawParameter = true;

    // 3. Execute callback if defined
    void (*funcPtr)(void) = (void (*)(void))primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].postUpdateFunc;
    if (funcPtr != NULL) {
        funcPtr();
    }
}
```

The order is important:

1. **First**, update the variable itself
2. **Second**, mark the display for redraw
3. **Third**, execute the callback

This ensures the callback sees the new value when it runs, and the display shows the updated value on the next frame.

### Safety Considerations

Callbacks execute in the main loop context, so they must complete within the 10ms loop timing budget. Callbacks that perform I2C transactions (like `UpdateRatten()`) are safe because the write-on-change optimization prevents redundant transactions. If you're rapidly rotating the encoder, most frames will skip I2C writes because the cached value hasn't changed.

Callbacks should also be idempotent — calling them multiple times with the same value should be safe and efficient. The RF hardware state management achieves this through state caching: if you set attenuation to 10 dB twice in a row, the second call does nothing.

## Menu Flow: Putting It All Together

Let's trace the complete user experience of adjusting a parameter, from button press to hardware update:

### Step-by-Step Flow

**1. User is on HOME screen** (UISm_StateId_HOME)

   - Main display shows spectrum, VFO, S-meter
   - Encoder rotation adjusts frequency or filter bandwidth

**2. User presses MENU button**

   - UI state machine transitions: HOME → MAIN_MENU
   - Display shows primary menu categories:
     - RF Options
     - CW Options
     - Calibration
     - Display
   - Selection indicator highlights "RF Options"

**3. User rotates encoder to navigate primary menu**

   - `IncrementPrimaryMenu()` or `DecrementPrimaryMenu()` updates `primaryMenuIndex`
   - Display updates to show different highlighted category
   - Still in MAIN_MENU state

**4. User presses SELECT button**

   - UI state machine transitions: MAIN_MENU → SECONDARY_MENU
   - Display shows secondary menu items for selected category:
     - SSB Power
     - CW Power
     - Gain
     - RX Attenuation (highlighted)
     - ...

**5. User rotates encoder to select "RX Attenuation"**

   - `IncrementSecondaryMenu()` or `DecrementSecondaryMenu()` updates `secondaryMenuIndex`
   - Display highlights different menu items
   - Still in SECONDARY_MENU state

**6. User presses SELECT button**

   - UI state machine transitions: SECONDARY_MENU → UPDATE
   - `UPDATE_enter()` sets `clearScreen = true`
   - `DrawDisplay()` renders:
     - HOME screen in background (Layer 1)
     - Parameter badge overlay: "RX Attenuation: 0" with red border (Layer 2)

**7. User rotates encoder clockwise**

   - Main loop detects iFILTER_INCREASE event
   - Routes to UPDATE state handler: `IncrementValue()`
   - `IncrementVariable(&rxAtten)` changes value from 0 to 0.5 dB
   - `redrawParameter = true` marks display for update
   - `UpdateRatten()` callback executes, updating RF board hardware
   - Next frame: badge shows "RX Attenuation: 0.5"
   - S-meter reading drops as RF attenuation increases

**8. User continues rotating encoder**

   - Each detent: value increases by 0.5 dB (the step size)
   - Each change: immediate hardware update via callback
   - User sees real-time S-meter response
   - Value stops at 31.5 dB (the maximum) even if encoder keeps rotating

**9. User presses HOME or SELECT button**

   - UI state machine transitions: UPDATE → HOME
   - Badge overlay disappears
   - Back to normal HOME screen
   - New attenuation value persists

### State Machine Diagram

```
    SPLASH
       ↓
     HOME ←──────────────────────┐
       ↓                         │
   [MENU button]                 │
       ↓                         │
   MAIN_MENU                     │
       ↓                         │
   [SELECT on category]          │
       ↓                         │
SECONDARY_MENU                   │
       ↓                         │
   [SELECT on item]              │
       ↓                         │
     UPDATE ───[HOME/SELECT]─────┘
       ↓
   [encoder rotation: increment/decrement value]
       ↓
   [callback executes if defined]
```

### Key Observations

1. **State-Driven Behavior**: The same physical encoder does different things in different states (navigate menu vs adjust value vs tune frequency).

2. **Overlay Rendering**: The UPDATE state overlays the parameter badge on top of the HOME screen, providing context (you can see if your changes affect the spectrum or S-meter).

3. **Immediate Feedback**: Callbacks ensure changes take effect in real-time, not at some later point.

4. **Bounds Enforcement**: The increment/decrement functions prevent invalid values, protecting hardware from damage.

## Design Benefits and Trade-offs

The Phoenix menu system's architecture provides several significant advantages, along with some trade-offs worth understanding.

### Benefits

**1. Extensibility Without Code Changes**

Adding a new menu parameter requires only data declarations, no logic changes:

```cpp
// Define the variable
int8_t newParameter = 50;

// Define its metadata
VariableParameter newParam = {
    .variable = &newParameter,
    .type = TYPE_I8,
    .limits.i8 = { .min = 0, .max = 100, .step = 1 }
};

// Add to a secondary menu
struct SecondaryMenuOption RFSet[8] = {  // Was [7], now [8]
    // ... existing items ...
    "New Parameter", variableOption, &newParam, NULL, (void *)SomeCallback,
};
```

The menu navigation code (`IncrementPrimaryMenu()`, `DrawSecondaryMenu()`, etc.) works unchanged. The menu arrays are iterated programmatically, so length changes are automatic.

**2. Type Safety Through Runtime Checks**

While C lacks compile-time generics, the `VariableParameter` design provides runtime type safety:

- Type tag ensures correct casting
- Type-specific limits prevent range errors
- Null pointer checks prevent crashes

This is safer than using raw pointers or macros, which could cast to incorrect types or apply invalid bounds.

**3. Separation of Concerns**

The menu system cleanly separates:

- **Data**: Menu structure (structs)
- **Navigation**: State machine transitions (UISm)
- **Display**: Rendering logic (DrawDisplay)
- **Modification**: Variable manipulation (IncrementVariable)
- **Hardware**: Callbacks (UpdateRatten, etc.)

Each component has a single responsibility, making the code easier to test and modify.

**4. Hardware Safety**

Bounds checking and callbacks work together to ensure safe hardware operation:

- Parameters can't exceed safe values
- Hardware updates happen immediately (no stale configuration)
- Idempotent operations prevent issues from rapid changes

**5. Efficient Display Updates**

The `redrawParameter` flag pattern minimizes screen updates:

- Only redraw when value changes
- No flicker from unnecessary redraws
- Keeps main loop under 10ms timing budget

### Trade-offs

**1. Void Pointer Type Erasure**

Using `void*` for `variable` and `func` pointers erases compile-time type information. The compiler can't catch:

- Incorrect function signatures in callbacks
- Type mismatches between variable and VariableParameter
- NULL pointer dereferences (caught at runtime instead)

This is a classic C trade-off: flexibility at the cost of compile-time safety.

**2. Memory Usage**

Each menu item stores function pointers even if not used (e.g., `func` is NULL for variable options, `varPam` is NULL for function options). This wastes a few bytes per item.

For a small menu system (dozens of items), this is negligible. For a very large menu (hundreds of items), union-based selective storage could save memory:

```cpp
struct SecondaryMenuOption {
    const char *label;
    optionType action;
    union {
        struct { VariableParameter *varPam; void *postUpdateFunc; } var;
        struct { void *func; } fn;
    } data;
};
```

The Phoenix code chooses simplicity over memory optimization, which is appropriate for this scale.

**3. No Compile-Time Menu Validation**

Menu structure errors are caught at runtime (e.g., NULL pointer crashes) rather than compile-time. Template-based systems (in C++) could provide compile-time validation, but would add complexity and reduce flexibility.

**4. Manual Index Management**

The code uses global `primaryMenuIndex` and `secondaryMenuIndex` variables to track menu position. This works well for a single-threaded embedded system, but wouldn't extend to multi-user or concurrent scenarios.

## Conclusion

The Phoenix SDR menu system demonstrates how carefully designed data structures can create a flexible, type-safe configuration interface in C. By combining:

- **VariableParameter structs** for generic variable metadata
- **SecondaryMenuOption structs** with dual action types
- **PrimaryMenuOption structs** for hierarchical organization
- **The UPDATE state** for real-time parameter adjustment
- **Post-update callbacks** for immediate hardware response

The system achieves a clean separation between menu navigation logic and the actual parameters being configured. Adding new parameters is straightforward, and the runtime type checking prevents common errors.

The UPDATE state is particularly elegant: it overlays a parameter badge on the HOME screen, allowing users to see real-time effects of their changes (spectrum shifts, S-meter readings, etc.) while adjusting values. The state machine ensures predictable transitions, and the callback mechanism bridges UI changes to hardware updates.

For embedded developers building configuration interfaces, this architecture offers a practical pattern: use structs to describe your UI, use function pointers for callbacks, and use state machines to manage transitions. The result is maintainable code where adding features doesn't require touching navigation logic.

## Code References

- Menu structure definitions: `code/src/PhoenixSketch/MainBoard_Display.h:6-54`
- Menu data definitions: `code/src/PhoenixSketch/MainBoard_Display.cpp:2347-2452`
- UI state machine (generated): `code/src/PhoenixSketch/UISm.h:1-91`, `code/src/PhoenixSketch/UISm.cpp:1-580`
- UPDATE state handlers: `code/src/PhoenixSketch/UISm.cpp:475-547`
- Variable manipulation: `code/src/PhoenixSketch/MainBoard_Display.cpp:2069-2261`
- Display rendering: `code/src/PhoenixSketch/MainBoard_Display.cpp:2905-2945`
- Increment/Decrement value wrappers: `code/src/PhoenixSketch/MainBoard_Display.cpp:2785-2819`
- Main loop event routing: `code/src/PhoenixSketch/Loop.cpp:684-724`
