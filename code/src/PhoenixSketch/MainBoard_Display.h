#ifndef DISPLAY_H
#define DISPLAY_H
#include "SDT.h"

// Constants
#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   480
#define DARKGREY 0x7BEF

/**
 * Display pane structure for modular screen regions.
 * Each pane represents a rectangular area with its own draw function.
 */
struct Pane {
    uint16_t x0;            ///< Top-left X coordinate
    uint16_t y0;            ///< Top-left Y coordinate
    uint16_t width;         ///< Width in pixels
    uint16_t height;        ///< Height in pixels
    void (*DrawFunction)(void);  ///< Function to render this pane
    bool stale;             ///< True if pane needs redrawing
};

/**
 * Simple rectangle structure for defining display regions.
 * Used for text bounding boxes and erase operations.
 */
struct Rectangle {
    uint16_t x0;            ///< Top-left X coordinate
    uint16_t y0;            ///< Top-left Y coordinate
    uint16_t width;         ///< Width in pixels
    uint16_t height;        ///< Height in pixels
};

/**
 * Display scale configuration for spectrum/waterfall display.
 * Defines dB scale parameters and pixel mapping.
 */
struct dispSc {
    const char *dbText;           ///< Text label for this scale (e.g., "10dB/div")
    float32_t dBScale;            ///< Number of dB per division
    uint16_t pixelsPerDB;         ///< Pixel height per dB
    uint16_t baseOffset;          ///< Base vertical offset in pixels
    float32_t offsetIncrement;    ///< Offset adjustment per step
};

// Helper function declarations

/**
 * @brief Calculate bounding rectangle for text at given position
 * @param x0 Starting X coordinate (top-left)
 * @param y0 Starting Y coordinate (top-left)
 * @param rect Output rectangle structure to fill
 * @param Nchars Number of characters in text string
 * @param charWidth Width of each character in pixels
 * @param charHeight Height of characters in pixels
 * @note Used to determine erase region before redrawing text
 */
void CalculateTextCorners(int16_t x0,int16_t y0,Rectangle *rect,int16_t Nchars,
                        uint16_t charWidth,uint16_t charHeight);

/**
 * @brief Erase rectangular area by filling with background color
 * @param rect Pointer to Rectangle defining area to blank
 * @note Clears display region before redrawing updated content
 */
void BlankBox(Rectangle *rect);

/**
 * Variable type enumeration for type-safe parameter handling.
 * Used by VariableParameter to support different data types in menu system.
 */
enum VarType {
    TYPE_I8,          ///< 8-bit signed integer
    TYPE_I16,         ///< 16-bit signed integer
    TYPE_I32,         ///< 32-bit signed integer
    TYPE_I64,         ///< 64-bit signed integer
    TYPE_F32,         ///< 32-bit floating point
    TYPE_KeyTypeId,   ///< CW key type enumeration
    TYPE_BOOL         ///< Boolean value
};

/**
 * Type-safe parameter descriptor for menu variable manipulation.
 * Encapsulates variable pointer, type information, and min/max/step constraints.
 * Enables generic increment/decrement operations with bounds checking.
 */
struct VariableParameter {
    void *variable;      ///< Pointer to the actual variable being controlled
    VarType type;        ///< Data type of the variable
    union {
        struct { int8_t min; int8_t max; int8_t step;} i8;           ///< Limits for TYPE_I8
        struct { int16_t min; int16_t max; int16_t step;} i16;       ///< Limits for TYPE_I16
        struct { int32_t min; int32_t max; int32_t step;} i32;       ///< Limits for TYPE_I32
        struct { int64_t min; int64_t max; int64_t step;} i64;       ///< Limits for TYPE_I64
        struct { float32_t min; float32_t max; float32_t step;} f32; ///< Limits for TYPE_F32
        struct { KeyTypeId min; KeyTypeId max; int8_t step;} keyType;///< Limits for TYPE_KeyTypeId
        struct { bool min; bool max; int8_t step;} b;                ///< Limits for TYPE_BOOL
    } limits;  ///< Min/max/step values specific to variable type
};

/**
 * @brief Increment a variable according to its type and limits
 * @param bv Pointer to VariableParameter structure defining variable and constraints
 * @note Automatically handles type casting and limit checking
 * @note Wraps around from max to min value
 */
void IncrementVariable(const VariableParameter *bv);

/**
 * @brief Decrement a variable according to its type and limits
 * @param bv Pointer to VariableParameter structure defining variable and constraints
 * @note Automatically handles type casting and limit checking
 * @note Wraps around from min to max value
 */
void DecrementVariable(const VariableParameter *bv);

/**
 * @brief Convert variable value to displayable string
 * @param vp Pointer to VariableParameter containing variable to format
 * @return Arduino String containing formatted value
 * @note Handles different variable types (int8_t, int32_t, float, bool, etc.)
 */
String GetVariableValueAsString(const VariableParameter *vp);

/**
 * Menu option action type enumeration.
 * Determines whether menu item adjusts a variable or calls a function.
 */
enum optionType {
    variableOption,   ///< Menu item controls a variable (with increment/decrement)
    functionOption    ///< Menu item executes a function when selected
};

/**
 * Secondary menu option descriptor.
 * Defines a single option within a submenu, either for variable adjustment
 * or function execution.
 */
struct SecondaryMenuOption {
    const char *label;              ///< Display text for this menu option
    optionType action;              ///< Type of action (variable or function)
    VariableParameter *varPam;      ///< Pointer to variable descriptor (if variableOption)
    void *func;                     ///< Function to call when selected (if functionOption)
    void *postUpdateFunc;           ///< Callback after variable update (optional)
};

/**
 * Primary menu category descriptor.
 * Defines a top-level menu category containing multiple secondary options.
 */
struct PrimaryMenuOption {
    const char *label;              ///< Display text for this category
    SecondaryMenuOption *secondary; ///< Array of secondary menu options
    size_t length;                  ///< Number of options in secondary array
};

// Core display functions (MainBoard_Display.cpp)

/**
 * @brief Main display update function called from main loop
 * @note Checks stale flags for all panes and redraws only those needing updates
 * @note Implements selective update for efficient display refresh
 */
void DrawDisplay(void);

/**
 * @brief Initialize display hardware and draw initial screen
 * @note Configures RA8875 display controller, initializes all panes
 * @note Displays splash screen then transitions to home screen
 */
void InitializeDisplay(void);

// Home screen functions (MainBoard_DisplayHome.cpp)

/**
 * @brief Draw the main home/operating screen
 * @note Displays VFO frequencies, spectrum, S-meter, SWR, and all operating parameters
 * @note This is the primary screen shown during normal radio operation
 */
void DrawHome(void);

/**
 * @brief Draw startup splash screen with logo and version
 * @note Displayed briefly during system initialization
 * @note Shows Phoenix SDR branding and firmware version
 */
void DrawSplash(void);

/**
 * @brief Draw parameter update overlay screen
 * @note Displays current parameter being adjusted (volume, filter, etc.)
 * @note Shown temporarily when user changes settings
 */
void DrawParameter(void);

// Menu system functions (MainBoard_DisplayMenus.cpp)

/**
 * @brief Draw primary (top-level) menu
 * @note Displays main menu categories (RF Settings, CW Options, Display, etc.)
 * @note Highlights currently selected menu item
 */
void DrawMainMenu(void);

/**
 * @brief Draw secondary (sub-menu) options
 * @note Displays detailed options within selected primary menu
 * @note Highlights currently selected menu item
 */
void DrawSecondaryMenu(void);

/**
 * @brief Move selection to next primary menu item
 * @note Cycles through top-level menu categories
 * @note Wraps around from last to first item
 */
void IncrementPrimaryMenu(void);

/**
 * @brief Move selection to next secondary menu item
 * @note Cycles through options within current submenu
 * @note Wraps around from last to first option
 */
void IncrementSecondaryMenu(void);

/**
 * @brief Move selection to previous primary menu item
 * @note Cycles backward through top-level menu categories
 * @note Wraps around from first to last item
 */
void DecrementPrimaryMenu(void);

/**
 * @brief Move selection to previous secondary menu item
 * @note Cycles backward through options within current submenu
 * @note Wraps around from first to last option
 */
void DecrementSecondaryMenu(void);

/**
 * @brief Increase value of currently selected menu parameter
 * @note Respects min/max limits and step size for each parameter type
 * @note Called when user rotates encoder clockwise
 */
void IncrementValue(void);

/**
 * @brief Decrease value of currently selected menu parameter
 * @note Respects min/max limits and step size for each parameter type
 * @note Called when user rotates encoder counter-clockwise
 */
void DecrementValue(void);

/**
 * @brief Update menu variable pointers after configuration changes
 * @note Refreshes references when band or mode changes affect menu options
 * @note Ensures menu displays current band-specific settings
 */
void UpdateArrayVariables(void);

/**
 * @brief Turn button presses into a new frequency selection
 * @note Runs when in the FREQ_ENTRY state
 * @note Called by Loop.cpp
 */
void InterpretFrequencyEntryButtonPress(int32_t button);

/**
 * @brief Draw the frequency entry keypad display
 * @note Shows numeric keypad and current frequency being entered
 * @note Displayed when user enters direct frequency input mode
 */
void DrawFrequencyEntryPad(void);

/**
 * @brief Get number of digits entered in frequency entry mode
 * @return Number of digits currently in the frequency string
 * @note Used for unit tests to verify frequency entry logic
 */
int8_t DFEGetNumDigits(void);

/**
 * @brief Get the current frequency entry string
 * @return Pointer to frequency string buffer
 * @note Used for unit tests to verify frequency entry state
 */
char * DFEGetFString(void);

/**
 * @brief Draw the audio equalizer adjustment interface
 * @note Displays graphical EQ with frequency bands and gain controls
 * @note Allows real-time adjustment of receive audio EQ settings
 */
void DrawEqualizerAdjustment(void);

/**
 * @brief Draw the frequency calibration interface
 * @note Displays controls for adjusting Si5351 clock generator calibration
 * @note Used to compensate for crystal frequency errors
 */
void DrawCalibrateFrequency(void);

/**
 * @brief Draw the receive IQ calibration interface
 * @note Displays controls for phase and gain balance adjustment
 * @note Minimizes unwanted sideband images in receive path
 */
void DrawCalibrateRXIQ(void);

/**
 * @brief Draw the transmit IQ calibration interface
 * @note Displays controls for TX phase and gain balance adjustment
 * @note Minimizes carrier and sideband leakage in transmit path
 */
void DrawCalibrateTXIQ(void);

/**
 * @brief Draw the CW power amplifier calibration interface
 * @note Displays controls for PA power level calibration
 * @note Maps power settings to actual RF output levels
 */
void DrawCalibratePower(void);

/**
 * @brief Start automatic RX IQ calibration procedure
 * @note Automatically adjusts phase and gain for optimal sideband rejection
 * @note Uses internal test signal for calibration
 */
void EngageRXIQAutotune(void);

/**
 * @brief Increase the frequency correction factor
 * @note Increments the Si5351 calibration value by current step size
 * @note Used during frequency calibration to adjust VFO accuracy
 */
void IncreaseFrequencyCorrectionFactor(void);

/**
 * @brief Decrease the frequency correction factor
 * @note Decrements the Si5351 calibration value by current step size
 * @note Used during frequency calibration to adjust VFO accuracy
 */
void DecreaseFrequencyCorrectionFactor(void);

/**
 * @brief Change the frequency correction adjustment increment
 * @note Cycles through different step sizes (1Hz, 10Hz, 100Hz, etc.)
 * @note Allows coarse and fine adjustment during calibration
 */
void ChangeFrequencyCorrectionFactorIncrement(void);

// External variable declarations (shared between display modules)
extern size_t primaryMenuIndex;
extern size_t secondaryMenuIndex;
extern struct PrimaryMenuOption primaryMenu[7];

#endif
