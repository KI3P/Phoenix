#ifndef DISPLAY_H
#define DISPLAY_H
#include "SDT.h"

// Constants
#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   480
#define NUMBER_OF_PANES 12
#define SPECTRUM_REFRESH_MS 200

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
void CalculateTextCorners(int16_t x0,int16_t y0,Rectangle *rect,int16_t Nchars,
                        uint16_t charWidth,uint16_t charHeight);
void BlankBox(Rectangle *rect);
void FormatFrequency(long freq, char *freqBuffer);
void UpdateSetting(uint16_t charWidth, uint16_t charHeight, uint16_t xoffset,
                   char *labelText, uint8_t NLabelChars,
                   char *valueText, uint8_t NValueChars,
                   uint16_t yoffset, bool redrawFunction, bool redrawValue);
int64_t GetCenterFreq_Hz();
int64_t GetLowerFreq_Hz();
int64_t GetUpperFreq_Hz();

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
void IncrementVariable(const VariableParameter *bv);
void DecrementVariable(const VariableParameter *bv);
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
void DrawDisplay(void);
void InitializeDisplay(void);

// Home screen functions (MainBoard_DisplayHome.cpp)
void DrawHome(void);
void DrawSplash(void);
void DrawParameter(void);

// Menu system functions (MainBoard_DisplayMenus.cpp)
void DrawMainMenu(void);
void DrawSecondaryMenu(void);
void IncrementPrimaryMenu(void);
void IncrementSecondaryMenu(void);
void DecrementPrimaryMenu(void);
void DecrementSecondaryMenu(void);
void IncrementValue(void);
void DecrementValue(void);
void UpdateArrayVariables(void);

// External variable declarations (shared between display modules)
extern size_t primaryMenuIndex;
extern size_t secondaryMenuIndex;
extern struct PrimaryMenuOption primaryMenu[6];
extern Pane* WindowPanes[NUMBER_OF_PANES];

#endif
