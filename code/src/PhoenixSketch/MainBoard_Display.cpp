#include "SDT.h"

/*
 * Functions in this file do two things only: 
 * 1) They read the state of the radio, 
 * 2) They draw on the display.
 * They do not change the value of any variable that is declared outside of this file.
 */

static void DrawSplash(){}
static void DrawHome(){}


struct dispSc displayScale[] = // *dbText,dBScale, pixelsPerDB, baseOffset, offsetIncrement
    {
        { "20 dB/", 10.0, 2, 24, 1.00 },
        { "10 dB/", 20.0, 4, 10, 0.50 },
        { "5 dB/", 40.0, 8, 58, 0.25 },
        { "2 dB/", 100.0, 20, 120, 0.10 },
        { "1 dB/", 200.0, 40, 200, 0.05 }
    };

/**
 * Show the value of a parameter. 
 */
static void DrawValue(void){
    Base *base = (Base *)uiSM.vars.uiUp;
    switch (base->type){
        case TYPE_INT32: {
            UIValueUpdateInt *foo = (UIValueUpdateInt *)uiSM.vars.uiUp;
            int32_t current_value = foo->getValueFunction();
            //Debug(current_value);
            break;
        }
        case TYPE_FLOAT: {
            UIValueUpdateFloat *foo = (UIValueUpdateFloat *)uiSM.vars.uiUp;
            float current_value = foo->getValueFunction();
            //Debug(current_value);
            break;
        }
    }
}

void DrawDisplay(void){
    switch (uiSM.state_id){
        case (UISm_StateId_SPLASH):{
            DrawSplash();
            break;
        }
        case (UISm_StateId_HOME):{
            DrawHome();
            break;
        }
        default:
            break;
    }
}

