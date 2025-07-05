#include "SDT.h"

#ifdef TESTMODE
static int32_t intVal;
int32_t GetInt(void){
    return intVal;
}
errno_t SetInt(int32_t val){
    intVal = val;
    return ESUCCESS;
}
// TEST FUNCTION, DO NOT USE IN PROD
UIValueUpdateInt uiRFScaleUpdate = {
    .base = TYPE_INT32,
    .getValueFunction = GetInt,
    .incrementValue = 1,
    .setValueFunction = SetInt
};
#endif

UIValueUpdateFloat uiRXGainUpdate = {
    .base = TYPE_FLOAT,
    .getValueFunction = GetRXAttenuator,
    .incrementValue = 0.5,
    .setValueFunction = SetRXAttenuator
};

UIValueUpdateFloat uiTXGainUpdate = {
    .base = TYPE_FLOAT,
    .getValueFunction = GetTXAttenuator,
    .incrementValue = 0.5,
    .setValueFunction = SetTXAttenuator
};

/**
 * Increment the value of a variable in the UI. Uses the UI state machine structure 
 * variable uiUp which provides the function that gets the current value, provides the
 * magnitude of the increment, and provides the function that sets the new value.
 */
void UIIncValue(void){
    Base *base = (Base *)uiSM.vars.uiUp;
    switch (base->type){
        case TYPE_INT32: {
            UIValueUpdateInt *foo = (UIValueUpdateInt *)uiSM.vars.uiUp;
            int32_t prior_value = foo->getValueFunction();
            int32_t new_value = prior_value + foo->incrementValue;
            foo->setValueFunction(new_value);
            break;
        }
        case TYPE_FLOAT: {
            UIValueUpdateFloat *foo = (UIValueUpdateFloat *)uiSM.vars.uiUp;
            float prior_value = foo->getValueFunction();
            float new_value = prior_value + foo->incrementValue;
            foo->setValueFunction(new_value);
            break;
        }
    }
}

/**
 * Decrement the value of a variable in the UI. Uses the UI state machine structure 
 * variable uiUp which provides the function that gets the current value, provides the
 * magnitude of the decrement, and provides the function that sets the new value.
 */
void UIDecValue(void){
    Base *base = (Base *)uiSM.vars.uiUp;
    switch (base->type){
        case TYPE_INT32: {
            UIValueUpdateInt *foo = (UIValueUpdateInt *)uiSM.vars.uiUp;
            int32_t prior_value = foo->getValueFunction();
            int32_t new_value = prior_value - foo->incrementValue;
            foo->setValueFunction(new_value);
            break;
        }
        case TYPE_FLOAT: {
            UIValueUpdateFloat *foo = (UIValueUpdateFloat *)uiSM.vars.uiUp;
            float prior_value = foo->getValueFunction();
            float new_value = prior_value - foo->incrementValue;
            foo->setValueFunction(new_value);
            break;
        }
    }
}
