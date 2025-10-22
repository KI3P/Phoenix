#ifndef DISPLAY_H
#define DISPLAY_H
#include "SDT.h"

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

void DrawDisplay(void);
void InitializeDisplay(void);
void IncrementPrimaryMenu(void);
void IncrementSecondaryMenu(void);
void DecrementPrimaryMenu(void);
void DecrementSecondaryMenu(void);
void IncrementValue(void);
void DecrementValue(void);

#endif
