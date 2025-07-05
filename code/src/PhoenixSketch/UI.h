#ifndef UI_H
#define UI_H

#ifndef __STDC_LIB_EXT1__
typedef int errno_t;
#endif

/**
 * @brief Define an object type
 * 
 * The UI code contains a void pointer to a struct that it uses to increment
 * or decrement a parameter. It needs to case the void pointer to the appropriate
 * struct before doing this. The first member of the struct being cast will be a 
 * struct that contains this enum; the code reads this and applies the right cast 
 */
typedef enum {
    TYPE_INT32,
    TYPE_FLOAT
} ObjectType;

typedef struct {
    ObjectType type;
} Base;

/** 
 * @brief Used by the menu system when adjusting the value of an int type variable
 * 
 * Note that the setValueFunction *MUST* perform a range check for valid values
 */ 
typedef struct {
    Base base; /**< struct that allows the UI code to cast a void pointer to the correct type */
    int32_t (*getValueFunction)(void); /**< Pointer to function that gets current value */
    int32_t incrementValue; /**< Increment amount */
    errno_t (*setValueFunction)(int32_t); /**< Pointer to function that sets updated value */
} UIValueUpdateInt;

/** 
 * @brief Used by the menu system when adjusting the value of a float type variable
 * 
 * Note that the setValueFunction *MUST* perform a range check for valid values
 */ 
typedef struct {
    Base base; /**< struct that allows the UI code to cast a void pointer to the correct type */
    float (*getValueFunction)(void); /**< Pointer to function that gets current value */
    float incrementValue; /**< Increment amount */
    errno_t (*setValueFunction)(float); /**< Pointer to function that sets updated value */
} UIValueUpdateFloat;

extern UIValueUpdateFloat uiRXGainUpdate;
extern UIValueUpdateFloat uiTXGainUpdate;
extern UIValueUpdateInt uiRFScaleUpdate;

void UIIncValue(void);
void UIDecValue(void);

#ifdef TESTMODE
int32_t GetInt(void);
errno_t SetInt(int32_t val);
#endif

#endif //UI_H