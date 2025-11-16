/**
 * @file MainBoard_DisplayMenus.cpp
 * @brief Menu system for Phoenix SDR radio configuration
 *
 * This module implements the complete hierarchical menu system including:
 * - Variable increment/decrement with type-safe bounds checking
 * - Menu structure definitions (primary and secondary menus)
 * - Menu navigation functions
 * - Menu rendering functions
 * - Parameter value adjustment handlers
 *
 * Menu Architecture:
 * - Primary menu: Top-level categories (RF Options, CW Options, etc.)
 * - Secondary menu: Options within each category
 * - UPDATE state: Value adjustment for selected parameter
 *
 * @see MainBoard_Display.h for menu structure definitions
 * @see MainBoard_Display.cpp for display infrastructure
 */

#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>

// External references to objects defined in MainBoard_Display.cpp
extern RA8875 tft;
extern bool redrawParameter;

///////////////////////////////////////////////////////////////////////////////
// VARIABLE MANIPULATION FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

/**
 * Increment a variable with type-safe bounds checking.
 */
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
        case TYPE_I32: {
            int32_t value = *(int32_t *)bv->variable;
            value = value + bv->limits.i32.step;
            if (value > bv->limits.i32.max){
                value = bv->limits.i32.max;
            }
            *(int32_t *)bv->variable = value;
            return;
        }
        case TYPE_I64: {
            int64_t value = *(int64_t *)bv->variable;
            value = value + bv->limits.i64.step;
            if (value > bv->limits.i64.max){
                value = bv->limits.i64.max;
            }
            *(int64_t *)bv->variable = value;
            return;
        }
        case TYPE_F32: {
            float32_t value = *(float32_t *)bv->variable;
            value = value + bv->limits.f32.step;
            if (value > bv->limits.f32.max){
                value = bv->limits.f32.max;
            }
            *(float32_t *)bv->variable = value;
            return;
        }
        case TYPE_KeyTypeId: {
            int8_t value = *(int8_t *)bv->variable;
            value = value + bv->limits.keyType.step;
            if (value > (int8_t)bv->limits.keyType.max){
                value = (int8_t)bv->limits.keyType.max;
            }
            *(KeyTypeId *)bv->variable = (KeyTypeId)value;
            return;
        }
        case TYPE_BOOL: {
            bool value = *(bool *)bv->variable;
            *(bool *)bv->variable = !value;
            return;
        }
        default:
            return;
    }
}

/**
 * Decrement a variable with type-safe bounds checking.
 */
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
        case TYPE_I16: {
            int16_t value = *(int16_t *)bv->variable;
            value = value - bv->limits.i16.step;
            if (value < bv->limits.i16.min){
                value = bv->limits.i16.min;
            }
            *(int16_t *)bv->variable = value;
            return;
        }
        case TYPE_I32: {
            int32_t value = *(int32_t *)bv->variable;
            value = value - bv->limits.i32.step;
            if (value < bv->limits.i32.min){
                value = bv->limits.i32.min;
            }
            *(int32_t *)bv->variable = value;
            return;
        }
        case TYPE_I64: {
            int64_t value = *(int64_t *)bv->variable;
            value = value - bv->limits.i64.step;
            if (value < bv->limits.i64.min){
                value = bv->limits.i64.min;
            }
            *(int64_t *)bv->variable = value;
            return;
        }
        case TYPE_F32: {
            float32_t value = *(float32_t *)bv->variable;
            value = value - bv->limits.f32.step;
            if (value < bv->limits.f32.min){
                value = bv->limits.f32.min;
            }
            *(float32_t *)bv->variable = value;
            return;
        }
        case TYPE_KeyTypeId: {
            int8_t value = *(int8_t *)bv->variable;
            value = value - bv->limits.keyType.step;
            if (value < (int8_t)bv->limits.keyType.min){
                value = (int8_t)bv->limits.keyType.min;
            }
            *(KeyTypeId *)bv->variable = (KeyTypeId)value;
            return;
        }
        case TYPE_BOOL: {
            bool value = *(bool *)bv->variable;
            *(bool *)bv->variable = !value;
            return;
        }
        default:
            return;
    }
}

/**
 * Get variable value as a String for display
 */
String GetVariableValueAsString(const VariableParameter *vp) {
    if (vp == NULL || vp->variable == NULL) {
        return String("NULL");
    }

    switch(vp->type) {
        case TYPE_I8:
            return String(*(int8_t*)vp->variable);
        case TYPE_I16:
            return String(*(int16_t*)vp->variable);
        case TYPE_I32:
            return String(*(int32_t*)vp->variable);
        case TYPE_I64:
            return String((long)*(int64_t*)vp->variable);
        case TYPE_F32:
            return String(*(float32_t*)vp->variable);
        case TYPE_KeyTypeId:
            return String(*(int8_t*)vp->variable);
        case TYPE_BOOL:
            return String(*(bool*)vp->variable ? "true" : "false");
        default:
            return String("UNKNOWN_TYPE");
    }
}

///////////////////////////////////////////////////////////////////////////////
// MENU STRUCTURE DEFINITIONS
///////////////////////////////////////////////////////////////////////////////

// RF Set Menu variable parameters
VariableParameter ssbPower = {
    .variable = NULL,
    .type = TYPE_F32,
    .limits = {.f32 = {.min = 0.0f, .max=20.0f, .step=0.5f}}
};

VariableParameter cwPower = {
    .variable = NULL,
    .type = TYPE_F32,
    .limits = {.f32 = {.min = 0.0f, .max=20.0f, .step=0.5f}}
};

VariableParameter gain = {
    .variable = &ED.rfGainAllBands_dB,
    .type = TYPE_F32,
    .limits = {.f32 = {.min = -5.0f, .max=20.0f, .step=0.5f}}
};

VariableParameter rxAtten = {
    .variable = NULL,
    .type = TYPE_F32,
    .limits = {.f32 = {.min = 0.0f, .max=31.5f, .step=0.5f}}
};

VariableParameter txAttenCW = {
    .variable = NULL,
    .type = TYPE_F32,
    .limits = {.f32 = {.min = 0.0f, .max=31.5f, .step=0.5f}}
};

VariableParameter txAttenSSB = {
    .variable = NULL,
    .type = TYPE_F32,
    .limits = {.f32 = {.min = 0.0f, .max=31.5f, .step=0.5f}}
};

VariableParameter antenna = {
    .variable = NULL,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 0, .max=3, .step=1}}
};

// Post-update callback functions for RF Set menu
void UpdateRatten(void){
    SetRXAttenuation(*(float32_t *)rxAtten.variable);
}

void UpdateTXAttenCW(void){
    SetTXAttenuation(*(float32_t *)txAttenCW.variable);
}

void UpdateTXAttenSSB(void){
    SetTXAttenuation(*(float32_t *)txAttenSSB.variable);
}

struct SecondaryMenuOption RFSet[7] = {
    "SSB Power", variableOption, &ssbPower, NULL, NULL,
    "CW Power", variableOption, &cwPower, NULL, NULL,
    "Gain",variableOption, &gain, NULL, NULL,
    "RX Attenuation",variableOption, &rxAtten, NULL, (void *)UpdateRatten,
    "TX Attenuation (CW)",variableOption, &txAttenCW, NULL, (void *)UpdateTXAttenCW,
    "TX Attenuation (SSB)",variableOption, &txAttenSSB, NULL, (void *)UpdateTXAttenSSB,
    "Antenna",variableOption, &antenna, NULL, (void *)UpdateTuneState,
};

// CW Options menu
VariableParameter wpm = {
    .variable = &ED.currentWPM,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 5, .max=50, .step=1}}
};

void SelectStraightKey(void){
    ED.keyType = KeyTypeId_Straight;
}

void SelectKeyer(void){
    ED.keyType = KeyTypeId_Keyer;
}

void FlipPaddle(void){
    ED.keyerFlip = !ED.keyerFlip;
}

VariableParameter cwf = {
    .variable = &ED.CWFilterIndex,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 0, .max=5, .step=1}}
};

VariableParameter stv = {
    .variable = &ED.sidetoneVolume,
    .type = TYPE_F32,
    .limits = {.f32 = {.min = 0.0F, .max=100.0F, .step=0.5F}}
};

struct SecondaryMenuOption CWOptions[6] = {
    "WPM", variableOption, &wpm, NULL, (void *)UpdateDitLength,
    "Straight key", functionOption, NULL, (void *)SelectStraightKey, NULL,
    "Keyer", functionOption, NULL, (void *)SelectKeyer, NULL,
    "Flip paddle", functionOption, NULL, (void *)FlipPaddle, NULL,
    "CW Filter", variableOption, &cwf, NULL, NULL,
    "Sidetone volume", variableOption, &stv, NULL, NULL,
};

// Audio Options
void SelectAGCOff(void){ ED.agc = AGCOff; }
void SelectAGCLong(void){ ED.agc = AGCLong; }
void SelectAGCSlow(void){ ED.agc = AGCSlow; }
void SelectAGCMedium(void){ ED.agc = AGCMed; }
void SelectAGCFast(void){ ED.agc = AGCFast; }
void ToggleAutonotch(void){
    if (ED.ANR_notchOn)
        ED.ANR_notchOn = 0;
    else 
        ED.ANR_notchOn = 1;
}
void SelectNROff(void){ ED.nrOptionSelect = NROff; }
void SelectNRKim(void){ ED.nrOptionSelect = NRKim; }
void SelectNRSpectral(void){ ED.nrOptionSelect = NRSpectral; }
void SelectNRLMS(void){ ED.nrOptionSelect = NRLMS; }

void StartEqualizerAdjust(void){
    SetInterrupt(iEQUALIZER);
}

struct SecondaryMenuOption AudioOptions[11] = {
    "AGC Off", functionOption, NULL, (void *)SelectAGCOff, NULL,
    "AGC Long", functionOption, NULL, (void *)SelectAGCLong, NULL,
    "AGC Slow", functionOption, NULL, (void *)SelectAGCSlow, NULL,
    "AGC Medium", functionOption, NULL, (void *)SelectAGCMedium, NULL,
    "AGC Fast", functionOption, NULL, (void *)SelectAGCFast, NULL,
    "Adjust Equalizers",functionOption, NULL, (void *)StartEqualizerAdjust, NULL,
    "Toggle Autonotch", functionOption, NULL, (void *)ToggleAutonotch, NULL,
    "Noise Reduction Off", functionOption, NULL, (void *)SelectNROff, NULL,
    "Kim Noise Reduction", functionOption, NULL, (void *)SelectNRKim, NULL,
    "Spectral Noise Reduc.", functionOption, NULL, (void *)SelectNRSpectral, NULL,
    "LMS Noise Reduction", functionOption, NULL, (void *)SelectNRLMS, NULL,
};

// Microphone Options
VariableParameter micg = {
    .variable = &ED.currentMicGain,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = -10, .max=30, .step=1}}
};

struct SecondaryMenuOption MicOptions[1] = {
    "Mic gain", variableOption, &micg, NULL, NULL,
};

// Calibration Menu
VariableParameter rflevelcal = {
    .variable = NULL, // will be set to &ED.dbm_calibration[ED.currentBand[ED.activeVFO]]
    .type = TYPE_F32,
    .limits = {.f32 = {.min = -20.0F, .max=50.0F, .step=0.5F}}
};

void StartFreqCal(void){
    SetInterrupt(iCALIBRATE_FREQUENCY);
}

void StartRXIQCal(void){
    SetInterrupt(iCALIBRATE_RX_IQ);  
}

void StartTXIQCal(void){
    SetInterrupt(iCALIBRATE_TX_IQ); 
}

void StartPowerCal(void){
    SetInterrupt(iCALIBRATE_CW_PA);   
}

struct SecondaryMenuOption CalOptions[5] = {
    "S meter level", variableOption, &rflevelcal, NULL, NULL,
    "Frequency", functionOption, NULL, (void *)StartFreqCal, NULL,
    "Receive IQ", functionOption, NULL, (void *)StartRXIQCal, NULL,
    "Transmit IQ", functionOption, NULL, (void *)StartTXIQCal, NULL,
    "Power", functionOption, NULL, (void *)StartPowerCal, NULL,
};

// Display menu
VariableParameter spectrumfloor = {
    .variable = NULL,
    .type = TYPE_I16,
    .limits = {.i16 = {.min = -100, .max=100, .step=1}}
};

VariableParameter spectrumscale = {
    .variable = &ED.spectrumScale,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 0, .max=4, .step=1}}
};

void ScaleUpdated(void){
    extern Pane PaneSpectrum;
    PaneSpectrum.stale = true;
}

struct SecondaryMenuOption DisplayOptions[2] = {
    "Spectrum floor", variableOption, &spectrumfloor, NULL, NULL,
    "Spectrum scale", variableOption, &spectrumscale, NULL, (void *)ScaleUpdated,
};

// EEPROM Menu
struct SecondaryMenuOption EEPROMOptions[3] = {
    "Save data to storage", functionOption, NULL, (void *)SaveDataToStorage, NULL,
    "Load from SD card", functionOption, NULL, (void *)RestoreDataFromSDCard, NULL,
    "Print data to Serial", functionOption, NULL, (void *)PrintEDToSerial, NULL,
};

// Primary menu structure
struct PrimaryMenuOption primaryMenu[7] = {
    "RF Options", RFSet, sizeof(RFSet)/sizeof(RFSet[0]),
    "CW Options", CWOptions, sizeof(CWOptions)/sizeof(CWOptions[0]),
    "Microphone", MicOptions, sizeof(MicOptions)/sizeof(MicOptions[0]),
    "Audio Options", AudioOptions, sizeof(AudioOptions)/sizeof(AudioOptions[0]),
    "Display", DisplayOptions, sizeof(DisplayOptions)/sizeof(DisplayOptions[0]),
    "EEPROM", EEPROMOptions, sizeof(EEPROMOptions)/sizeof(EEPROMOptions[0]),
    "Calibration", CalOptions, sizeof(CalOptions)/sizeof(CalOptions[0]),
};

/**
 * Update menu variable pointers to reference current band-specific values.
 */
void UpdateArrayVariables(void){
    ssbPower.variable = &ED.powerOutSSB[ED.currentBand[ED.activeVFO]];
    cwPower.variable = &ED.powerOutCW[ED.currentBand[ED.activeVFO]];
    rxAtten.variable = &ED.RAtten[ED.currentBand[ED.activeVFO]];
    txAttenCW.variable = &ED.XAttenCW[ED.currentBand[ED.activeVFO]];
    txAttenSSB.variable = &ED.XAttenSSB[ED.currentBand[ED.activeVFO]];
    antenna.variable = &ED.antennaSelection[ED.currentBand[ED.activeVFO]];
    spectrumfloor.variable = &ED.spectrumNoiseFloor[ED.currentBand[ED.activeVFO]];
    rflevelcal.variable = &ED.dbm_calibration[ED.currentBand[ED.activeVFO]];
}

///////////////////////////////////////////////////////////////////////////////
// MENU NAVIGATION FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

// State tracking for menu navigation
size_t primaryMenuIndex = 0;     // Current primary (category) menu selection
size_t secondaryMenuIndex = 0;   // Current secondary (option) menu selection
bool redrawMenu = true;          // Flag to trigger menu redraw

/**
 * Advance to the next primary menu category (with wrap-around).
 */
void IncrementPrimaryMenu(void){
    primaryMenuIndex++;
    if (primaryMenuIndex >= sizeof(primaryMenu)/sizeof(primaryMenu[0]))
        primaryMenuIndex = 0;
    secondaryMenuIndex = 0;
    redrawMenu = true;
}

/**
 * Move to the previous primary menu category (with wrap-around).
 */
void DecrementPrimaryMenu(void){
    if (primaryMenuIndex == 0)
        primaryMenuIndex = sizeof(primaryMenu)/sizeof(primaryMenu[0]) - 1;
    else
        primaryMenuIndex--;
    secondaryMenuIndex = 0;
    redrawMenu = true;
}

/**
 * Advance to the next secondary menu option within current category (with wrap-around).
 */
void IncrementSecondaryMenu(void){
    secondaryMenuIndex++;
    if (secondaryMenuIndex >= primaryMenu[primaryMenuIndex].length)
        secondaryMenuIndex = 0;
    redrawMenu = true;
}

/**
 * Move to the previous secondary menu option within current category (with wrap-around).
 */
void DecrementSecondaryMenu(void){
    if (secondaryMenuIndex == 0)
        secondaryMenuIndex = primaryMenu[primaryMenuIndex].length - 1;
    else
        secondaryMenuIndex--;
    redrawMenu = true;
}

/**
 * Increment the value of the currently selected menu parameter.
 */
void IncrementValue(void){
    IncrementVariable(primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].varPam);
    redrawParameter = true;
    void (*funcPtr)(void) = (void (*)(void))primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].postUpdateFunc;
    if (funcPtr != NULL) {
        funcPtr();
    }
}

/**
 * Decrement the value of the currently selected menu parameter.
 */
void DecrementValue(void){
    DecrementVariable(primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].varPam);
    redrawParameter = true;
    void (*funcPtr)(void) = (void (*)(void))primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].postUpdateFunc;
    if (funcPtr != NULL) {
        funcPtr();
    }
}

///////////////////////////////////////////////////////////////////////////////
// MENU RENDERING FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

void PrintMainMenuOptions(bool foreground){
    int16_t x = 10;
    int16_t y = 20;
    int16_t delta = 27;

    if (foreground)
        tft.setTextColor(RA8875_WHITE);
    else
        tft.setTextColor(DARKGREY, RA8875_BLACK);
    tft.setFontDefault();
    tft.setFontScale(1);

    for (size_t k=0; k<sizeof(primaryMenu)/sizeof(primaryMenu[0]); k++){
        if (k == primaryMenuIndex){
            if (foreground)
                tft.setTextColor(RA8875_GREEN);
            else
                tft.setTextColor(RA8875_WHITE);
        }
        tft.setCursor(x,y);
        tft.print(primaryMenu[k].label);
        if (k == primaryMenuIndex){
            if (foreground)
                tft.setTextColor(RA8875_WHITE);
            else
                tft.setTextColor(DARKGREY, RA8875_BLACK);
        }
        y += delta;
    }
    // Show the git commit
    tft.setFontScale(0);
    String msg = String("Git: ") + GIT_COMMIT_HASH;
    tft.setCursor(x, 460 - delta);
    tft.setTextColor(RA8875_WHITE);
    tft.print(msg);
}

void PrintSecondaryMenuOptions(bool foreground){
    int16_t x = 300;
    int16_t y = 20;
    int16_t delta = 27;

    if (foreground)
        tft.setTextColor(RA8875_WHITE);
    else
        tft.setTextColor(DARKGREY, RA8875_BLACK);
    tft.setFontDefault();
    tft.setFontScale(1);

    for (size_t m=0; m<primaryMenu[primaryMenuIndex].length; m++){
        if (m == secondaryMenuIndex){
            if (foreground)
                tft.setTextColor(RA8875_GREEN);
            else
               tft.setTextColor(DARKGREY, RA8875_BLACK);
        }
        tft.setCursor(x,y);
        tft.print(primaryMenu[primaryMenuIndex].secondary[m].label);
        if (m == secondaryMenuIndex){
            if (foreground)
                tft.setTextColor(RA8875_WHITE);
            else
                tft.setTextColor(DARKGREY, RA8875_BLACK);
        }
        y += delta;
    }
}

// State tracking for array variable updates (shared with MainBoard_DisplayHome.cpp)
uint8_t oavfo = 7;   // Previous active VFO value
int32_t oband = -1;  // Previous band value

void DrawMainMenu(void){
    if (!(uiSM.state_id == UISm_StateId_MAIN_MENU)) return;
    if (uiSM.vars.clearScreen){
        tft.writeTo(L2);
        tft.fillRect(1, 5, 650, 460, RA8875_BLACK);
        tft.writeTo(L1);

        uiSM.vars.clearScreen = false;
        redrawMenu = true;
    }
    if (!redrawMenu)
        return;
    redrawMenu = false;
    tft.fillRect(1, 5, 650, 460, RA8875_BLACK);
    tft.drawRect(1, 5, 650, 460, RA8875_YELLOW);

    if ((oavfo != ED.activeVFO) || (oband != ED.currentBand[ED.activeVFO])){
        oavfo = ED.activeVFO;
        oband = ED.currentBand[ED.activeVFO];
        UpdateArrayVariables();
    }

    PrintMainMenuOptions(true);
    PrintSecondaryMenuOptions(false);

}

void DrawSecondaryMenu(void){
    if (!(uiSM.state_id == UISm_StateId_SECONDARY_MENU)) return;
    if (uiSM.vars.clearScreen){
        uiSM.vars.clearScreen = false;
        redrawMenu = true;
    }

    if (!redrawMenu)
        return;
    redrawMenu = false;

    tft.fillRect(1,  5, 650, 460, RA8875_BLACK);
    tft.drawRect(1,  5, 650, 460, RA8875_YELLOW);

    PrintMainMenuOptions(false);
    PrintSecondaryMenuOptions(true);
}
