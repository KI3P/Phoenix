#ifndef DISPLAY_H
#define DISPLAY_H
#include "SDT.h"

// Constants
#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   480
#define DARKGREY 0x7BEF

// Forward declarations and structures
struct Pane {
    uint16_t x0;
    uint16_t y0;
    uint16_t width;
    uint16_t height;
    void (*DrawFunction)(void);
    bool stale;
};

struct Rectangle {
    uint16_t x0;
    uint16_t y0;
    uint16_t width;
    uint16_t height;
};

struct dispSc {
    const char *dbText;
    float32_t dBScale;
    uint16_t pixelsPerDB;
    uint16_t baseOffset;
    float32_t offsetIncrement;
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

// The variable being changed will have a type
enum VarType {
    TYPE_I8,
    TYPE_I16,
    TYPE_I32,
    TYPE_I64,
    TYPE_F32,
    TYPE_KeyTypeId,
    TYPE_BOOL
};

// We need a struct that tells us how to change the variable
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

// Menu option types
enum optionType {
    variableOption,
    functionOption
};

// Secondary menu option structure
struct SecondaryMenuOption {
    const char *label;
    optionType action;
    VariableParameter *varPam; // pointer to the variable, if appropriate
    void *func; // pointer to the function to call, if appropriate
    void *postUpdateFunc; // pointer to a function to call after the variable was update, if appropriate
};

// Primary menu option structure
struct PrimaryMenuOption {
    const char *label;
    SecondaryMenuOption *secondary;
    size_t length;
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

void DrawFrequencyEntryPad(void);
// Used for unit tests
int8_t DFEGetNumDigits(void);
char * DFEGetFString(void);

void DrawEqualizerAdjustment(void);

void DrawCalibrateFrequency(void);
void DrawCalibrateRXIQ(void);
void DrawCalibrateTXIQ(void);
void DrawCalibratePower(void);
void EngageRXIQAutotune(void);

void IncreaseFrequencyCorrectionFactor(void);
void DecreaseFrequencyCorrectionFactor(void);
void ChangeFrequencyCorrectionFactorIncrement(void);

// External variable declarations (shared between display modules)
extern size_t primaryMenuIndex;
extern size_t secondaryMenuIndex;
extern struct PrimaryMenuOption primaryMenu[7];

#endif
