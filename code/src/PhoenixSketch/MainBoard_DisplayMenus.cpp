/* 
Copyright (C) 2026 T41 EP Software Contributors
See Contributors.txt for list of known authors.

This file is part of Phoenix.

Phoenix is free software: you can redistribute it and/or modify it under the 
terms of the GNU General Public License as published by the Free Software 
Foundation, either version 3 of the License, or (at your option) any later version.

Phoenix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Phoenix. 
If not, see <https://www.gnu.org/licenses/>.
*/

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

//VariableParameter txAttenSSB = {
//    .variable = NULL,
//    .type = TYPE_F32,
//    .limits = {.f32 = {.min = 0.0f, .max=31.5f, .step=0.5f}}
//};

VariableParameter antenna = {
    .variable = NULL,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 0, .max=3, .step=1}}
};

/**
 * Post-update callback to apply RX attenuation value to hardware.
 * Called after rxAtten variable is modified via menu.
 */
void UpdateRatten(void){
    SetRXAttenuation(*(float32_t *)rxAtten.variable);
    SetInterrupt(iPOWER_CHANGE);
}

/**
 * Post-update callback to apply TX attenuation value for CW mode.
 * Called after txAttenCW variable is modified via menu.
 */
void UpdateTXAttenCW(void){
    // ED.Xatten[band] was changed. Change the set power so they are consistent
    float32_t att_dB = *(float32_t *)txAttenCW.variable;
    float32_t p = CalculateCWPowerLevel(att_dB,ED.PA100Wactive);
    if ((p >= 0) && (p <= 100*1000)){
        ED.powerOutCW[ED.currentBand[ED.activeVFO]] = p*1000;
        Debug("TXAtten "+String(att_dB)+"dB gives power [W]:"+String(p));
    } else {
        Debug("Updating TXAtten resulted in invalid power [W]:"+String(p));
    }
    SetInterrupt(iPOWER_CHANGE);
}

void UpdateSSBPower(void){
    float32_t setPower = *(float32_t *)ssbPower.variable;
    // Gain is calculated by DSP chain... do nothing here
    Debug("SSB set power is " + String(setPower));
}

void UpdateCWPower(void){
    float32_t setPower_W = *(float32_t *)cwPower.variable;
    bool psel;
    float32_t att_dB = CalculateCWAttenuation(setPower_W,&psel);
    if ((att_dB >= 0) && (att_dB < 32)){
        ED.XAttenCW[ED.currentBand[ED.activeVFO]] = att_dB;
        //Debug("Power "+String(setPower_W)+"W gives attenuation [dB]:"+String(att_dB));
    } else {
        Debug("Updating power resulted in invalid atten [dB]:"+String(att_dB));
    }
    SetInterrupt(iPOWER_CHANGE);
}

struct SecondaryMenuOption RFSet[6] = {
    "SSB Power", variableOption, &ssbPower, NULL, (void *)UpdateSSBPower,
    "CW Power", variableOption, &cwPower, NULL, (void *)UpdateCWPower,
    "RX Attenuation",variableOption, &rxAtten, NULL, (void *)UpdateRatten,
    "Antenna",variableOption, &antenna, NULL, (void *)UpdateTuneState,
    //"[__RX DSP Gain]",variableOption, &gain, NULL, NULL,
    //"[__TX Attenuation(CW)]",variableOption, &txAttenCW, NULL, (void *)UpdateTXAttenCW,
};

// CW Options menu
VariableParameter wpm = {
    .variable = &ED.currentWPM,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 5, .max=50, .step=1}}
};

/**
 * Menu callback to select straight key mode for CW operation.
 * Sets keyType to KeyTypeId_Straight in the EEPROM data structure.
 */
void SelectStraightKey(void){
    ED.keyType = KeyTypeId_Straight;
}

/**
 * Menu callback to select electronic keyer mode for CW operation.
 * Sets keyType to KeyTypeId_Keyer in the EEPROM data structure.
 */
void SelectKeyer(void){
    ED.keyType = KeyTypeId_Keyer;
}

/**
 * Menu callback to flip the paddle configuration (dit/dah swap).
 * Toggles the keyerFlip flag in the EEPROM data structure.
 */
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

/**
 * Menu callback to disable Automatic Gain Control (AGC).
 * Sets AGC mode to AGCOff in the EEPROM data structure.
 */
void SelectAGCOff(void){ ED.agc = AGCOff; }

/**
 * Menu callback to set AGC to Long mode (slowest attack/release).
 * Sets AGC mode to AGCLong in the EEPROM data structure.
 */
void SelectAGCLong(void){ ED.agc = AGCLong; }

/**
 * Menu callback to set AGC to Slow mode.
 * Sets AGC mode to AGCSlow in the EEPROM data structure.
 */
void SelectAGCSlow(void){ ED.agc = AGCSlow; }

/**
 * Menu callback to set AGC to Medium mode.
 * Sets AGC mode to AGCMed in the EEPROM data structure.
 */
void SelectAGCMedium(void){ ED.agc = AGCMed; }

/**
 * Menu callback to set AGC to Fast mode (fastest attack/release).
 * Sets AGC mode to AGCFast in the EEPROM data structure.
 */
void SelectAGCFast(void){ ED.agc = AGCFast; }

/**
 * Menu callback to toggle the automatic notch filter on/off.
 * Flips the ANR_notchOn flag in the EEPROM data structure.
 */
void ToggleAutonotch(void){
    if (ED.ANR_notchOn)
        ED.ANR_notchOn = 0;
    else
        ED.ANR_notchOn = 1;
}
/**
 * Menu callback to disable noise reduction.
 * Sets noise reduction mode to NROff in the EEPROM data structure.
 */
void SelectNROff(void){ ED.nrOptionSelect = NROff; }

/**
 * Menu callback to enable Kim noise reduction algorithm.
 * Sets noise reduction mode to NRKim in the EEPROM data structure.
 */
void SelectNRKim(void){ ED.nrOptionSelect = NRKim; }

/**
 * Menu callback to enable spectral noise reduction algorithm.
 * Sets noise reduction mode to NRSpectral in the EEPROM data structure.
 */
void SelectNRSpectral(void){ ED.nrOptionSelect = NRSpectral; }

/**
 * Menu callback to enable LMS (Least Mean Squares) noise reduction algorithm.
 * Sets noise reduction mode to NRLMS in the EEPROM data structure.
 */
void SelectNRLMS(void){ ED.nrOptionSelect = NRLMS; }

/**
 * Menu callback to start the equalizer adjustment interface.
 * Triggers an interrupt to enter the equalizer adjustment mode.
 */
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

/**
 * Menu callback to start frequency calibration process.
 * Triggers an interrupt to enter the frequency calibration mode.
 */
void StartFreqCal(void){
    SetInterrupt(iCALIBRATE_FREQUENCY);
}

/**
 * Menu callback to start receive IQ calibration process.
 * Triggers an interrupt to enter the RX IQ calibration mode.
 */
void StartRXIQCal(void){
    SetInterrupt(iCALIBRATE_RX_IQ);
}

/**
 * Menu callback to start transmit IQ calibration process.
 * Triggers an interrupt to enter the TX IQ calibration mode.
 */
void StartTXIQCal(void){
    SetInterrupt(iCALIBRATE_TX_IQ);
}

/**
 * Menu callback to start power amplifier calibration process.
 * Triggers an interrupt to enter the PA power calibration mode.
 */
void StartPowerCal(void){
    SetInterrupt(iCALIBRATE_POWER);
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

/**
 * Post-update callback when spectrum scale is modified.
 * Marks the spectrum pane as stale to trigger a redraw with the new scale.
 */
void ScaleUpdated(void){
    extern Pane PaneSpectrum;
    PaneSpectrum.stale = true;
}

void EnableAutonoisefloor(void){
    ED.spectrumFloorAuto = 1;
}
void DisableAutonoisefloor(void){
    ED.spectrumFloorAuto = 0;
}

struct SecondaryMenuOption DisplayOptions[4] = {
    "Auto spectrum floor", functionOption, NULL, (void *)EnableAutonoisefloor, NULL,
    "Manual spectrum floor", functionOption, NULL, (void *)DisableAutonoisefloor, NULL,
    "Spectrum floor", variableOption, &spectrumfloor, NULL, NULL,
    "Spectrum scale", variableOption, &spectrumscale, NULL, (void *)ScaleUpdated,
};

// EEPROM Menu
struct SecondaryMenuOption EEPROMOptions[3] = {
    "Save data to storage", functionOption, NULL, (void *)SaveDataToStorage, NULL,
    "Load from SD card", functionOption, NULL, (void *)RestoreDataFromSDCard, NULL,
    "Print data to Serial", functionOption, NULL, (void *)PrintEDToSerial, NULL,
};

// Diagnostic Menu
void GotoDisplayBIT(void){
    SetInterrupt(iBITDISPLAY);
}

void RestoreReceiveEQ(void){
    for (size_t k = 0; k < EQUALIZER_CELL_COUNT; k++){
        ED.equalizerRec[k] = 100;
    }
}

struct SecondaryMenuOption DiagnosticOptions[3] = {
    "I2C BIT test", functionOption, NULL, (void *)GotoDisplayBIT, NULL,
    "Restore receive EQ", functionOption, NULL, (void *)RestoreReceiveEQ, NULL,
    "Buffer print", functionOption, NULL, (void *)buffer_pretty_print_last_entry, NULL,
};

///////////////////////////////////////////////////////////////////////////////
// FT8 menu page
///////////////////////////////////////////////////////////////////////////////
//
// Runtime knobs and a one-click message-queue interface for FT8 operating.
// Callsign and grid are read from ED (set via Config.h MY_CALL or programmatic
// assignment); on-radio text editing without a USB keyboard is impractical
// so this menu intentionally skips a callsign/grid editor. The preset queue
// templates substitute <CALL>/<GRID> at queue time so changes take effect
// immediately without restart.
//
#include "DSP_FT8.h"

VariableParameter ft8TxFreqVar = {
    .variable = &ft8TxFreq,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 200, .max = 2700, .step = 5}}
};
VariableParameter ft8RxFreqVar = {
    .variable = &ft8RxFreq,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 200, .max = 2700, .step = 5}}
};
VariableParameter ft8TxEqRxVar = {
    .variable = &txEqualsRx,
    .type = TYPE_BOOL,
    .limits = {.b = {.min = false, .max = true, .step = 1}}
};
VariableParameter ft8TxStateVar = {
    .variable = &ft8TxState,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 0, .max = 1, .step = 1}}
};
VariableParameter ft8IntStateVar = {
    .variable = &ft8IntState,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 0, .max = 1, .step = 1}}
};
VariableParameter ft8CqStateVar = {
    .variable = &ft8CqState,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 0, .max = 1, .step = 1}}
};

/* functionOption requires a void(*)(void); wrap each preset slot. */
static void FT8MenuQueueSlot0(void){ FT8QueueMessageSlot(0); }
static void FT8MenuQueueSlot1(void){ FT8QueueMessageSlot(1); }
static void FT8MenuQueueSlot2(void){ FT8QueueMessageSlot(2); }
static void FT8MenuQueueSlot3(void){ FT8QueueMessageSlot(3); }
static void FT8MenuQueueSlot4(void){ FT8QueueMessageSlot(4); }
/* Cancel any pending or in-flight TX. Useful if the operator queues by
 * accident or wants to reconfigure mid-cycle. */
static void FT8MenuCancelTx(void){ FT8CancelTx(); }
/* Retune the active VFO to the current band's standard FT8 calling
 * frequency (e.g., 14.074 MHz on 20 m). Modulation isn't changed. */
static void FT8MenuTuneToBandFreq(void){ FT8TuneToBandFreq(); }
/* One-click "Go to FT8": switches modulation to FT8_INTERNAL AND retunes
 * to the current band's FT8 calling freq. Quickest way to get on the air. */
static void FT8MenuGoToModeAndTune(void){ FT8GoToModeAndTune(); }

/* USB-keyboard text editor entry points. Each spawns the editor with the
 * appropriate target string + label + on-commit save callback. The save
 * callback fires only on successful commit (MENU_OPTION_SELECT button);
 * cancel via HOME_SCREEN does not save. */
#include "MainBoard_TextEditor.h"
#include "Storage.h"

static void FT8MenuSaveAfterCommit(void){
    /* Commit callback shared by callsign + grid editors. Auto-save to
     * LittleFS so the change persists immediately. ~tens of ms; acceptable
     * for an interactive edit. */
    SaveDataToStorage(false);
}

static void FT8MenuEditCallsign(void){
    TextEditorBegin(ED.callsign, sizeof(ED.callsign),
                    "Edit Callsign:", FT8MenuSaveAfterCommit);
}

static void FT8MenuEditGrid(void){
    TextEditorBegin(ED.grid, sizeof(ED.grid),
                    "Edit Grid:", FT8MenuSaveAfterCommit);
}

/* Target-callsign tracking: capture the latest received CQ as the QSO
 * target so reply templates fill in correctly. The auto-update in
 * AddDecodedMessage only fires when target is empty, so this menu entry
 * lets the operator force-pull the latest CQ (e.g. when changing QSO
 * partner mid-session). */
static void FT8MenuTargetLatestCQ(void){ FT8TargetLatestCQ(); }
static void FT8MenuClearTarget(void){    FT8ClearTarget();    }
/* Edit the target string directly with the USB-keyboard editor. Useful
 * for typing a callsign without waiting for a CQ. */
static void FT8MenuEditTarget(void){
    TextEditorBegin(ft8TargetCall, sizeof(ft8TargetCall),
                    "Edit Target:", NULL);  /* not persisted to ED */
}

struct SecondaryMenuOption FT8Options[] = {
    /* Runtime knobs. The preset labels for slots use the same labels as
     * FT8GetPresetLabel returns; we hardcode them here to avoid a runtime
     * lookup since the menu label expects a stable const char *. */
    "TX Freq Hz",     variableOption, &ft8TxFreqVar,    NULL, NULL,
    "RX Freq Hz",     variableOption, &ft8RxFreqVar,    NULL, NULL,
    "TX = RX",        variableOption, &ft8TxEqRxVar,    NULL, NULL,
    "TX Mode (0/1)",  variableOption, &ft8TxStateVar,   NULL, NULL,
    "Interval (0=ev)",variableOption, &ft8IntStateVar,  NULL, NULL,
    "CQ Mode (0=man)",variableOption, &ft8CqStateVar,   NULL, NULL,
    /* One-click message queue. Each entry copies the corresponding template
     * (with <CALL>/<GRID> expanded) into txBuf[0] and marks it WAITING --
     * the slot-aligned TX gate in DSP_FT8.cpp will pick it up at the next
     * matching slot boundary. */
    "Queue: CQ",      functionOption, NULL, (void *)FT8MenuQueueSlot0, NULL,
    "Queue: 73",      functionOption, NULL, (void *)FT8MenuQueueSlot1, NULL,
    "Queue: RR73",    functionOption, NULL, (void *)FT8MenuQueueSlot2, NULL,
    "Queue: 599",     functionOption, NULL, (void *)FT8MenuQueueSlot3, NULL,
    "Queue: ID",      functionOption, NULL, (void *)FT8MenuQueueSlot4, NULL,
    "Cancel TX",      functionOption, NULL, (void *)FT8MenuCancelTx,   NULL,
    "Tune to FT8 freq", functionOption, NULL, (void *)FT8MenuTuneToBandFreq, NULL,
    "Go to FT8 + tune", functionOption, NULL, (void *)FT8MenuGoToModeAndTune, NULL,
    "Edit Callsign",  functionOption, NULL, (void *)FT8MenuEditCallsign,    NULL,
    "Edit Grid",      functionOption, NULL, (void *)FT8MenuEditGrid,        NULL,
    "Target latest CQ", functionOption, NULL, (void *)FT8MenuTargetLatestCQ, NULL,
    "Edit Target",    functionOption, NULL, (void *)FT8MenuEditTarget,      NULL,
    "Clear Target",   functionOption, NULL, (void *)FT8MenuClearTarget,     NULL,
};

///////////////////////////////////////////////////////////////////////////////
// PSK31 menu page
///////////////////////////////////////////////////////////////////////////////
//
// PSK31's settings surface is small: the audio receive frequency (also
// adjustable via the fine-tune encoder when in PSK31 mode), and a couple
// of one-shot reset functions for the decoder pipeline + text buffer.
//
#include "DSP_PSK31.h"

VariableParameter psk31RxFreqVar = {
    .variable = &psk31RxFreq,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 200, .max = 2700, .step = 5}}
};

/* functionOption requires void(*)(void); wrap each PSK31 action. */
static void PSK31MenuClearText(void)     { PSK31ClearText(); }
static void PSK31MenuResetDecoder(void)  { ResetPSK31Pipeline(); }

struct SecondaryMenuOption PSK31Options[] = {
    "RX Freq Hz",     variableOption, &psk31RxFreqVar, NULL, NULL,
    "Clear text",     functionOption, NULL, (void *)PSK31MenuClearText,    NULL,
    "Reset decoder",  functionOption, NULL, (void *)PSK31MenuResetDecoder, NULL,
};

///////////////////////////////////////////////////////////////////////////////
// DMR menu page
///////////////////////////////////////////////////////////////////////////////
//
// Operator-facing knobs for the DMR digital coding overlay (4FSK over
// NFM / FM_WIDE). The values are persisted in ED by Storage; each edit
// also calls SyncDmrConfigFromED() so the live runtime config (which
// the DSP, modem, and framing layers consult) stays current.
//
// Private-call dialing, RX TG whitelist, and per-VFO assignment are
// future enhancements -- this surface covers the minimum needed to take
// a Phoenix radio onto a Talk Group.
//
#include "DmrFraming.h"   // SyncDmrConfigFromED()

VariableParameter dmrEnableVar = {
    .variable = &ED.dmrEnabled,
    .type = TYPE_BOOL,
    .limits = {.b = {.min = false, .max = true, .step = 1}}
};
VariableParameter dmrTalkGroupVar = {
    .variable = &ED.dmrTalkGroup,
    .type = TYPE_I32,
    /* 24-bit DMR Group Address space; 0..16777215. Step 1; the operator
     * can wind through quickly with the encoder, and most popular TGs
     * (e.g. 9, 91, 310) sit in the low integers. */
    .limits = {.i32 = {.min = 0, .max = 16777215, .step = 1}}
};
VariableParameter dmrSrcIdVar = {
    .variable = &ED.dmrSrcId,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 0, .max = 16777215, .step = 1}}
};
VariableParameter dmrTimeSlotVar = {
    .variable = &ED.dmrTimeSlot,
    .type = TYPE_I32,
    .limits = {.i32 = {.min = 1, .max = 2, .step = 1}}
};
VariableParameter dmrColorCodeVar = {
    .variable = &ED.dmrColorCode,
    .type = TYPE_I8,
    .limits = {.i8 = {.min = 0, .max = 15, .step = 1}}
};

/* Post-update callback shared by every DMR menu entry. After the
 * encoder has incremented/decremented the underlying ED field, push the
 * value into the live `dmrConfig` global and persist to LittleFS so the
 * setting survives reboot. */
static void DmrMenuApplyAndSave(void){
    SyncDmrConfigFromED();
    SaveDataToStorage(false);
}

/* Save Now: explicit save trigger, useful after a sequence of edits. */
static void DmrMenuSaveNow(void){
    SaveDataToStorage(false);
}

/* Reset Defaults: restore the DMR config fields to the compile-time
 * defaults declared in SDT.h. Live runtime + persistent storage are
 * both updated in one shot. */
static void DmrMenuResetDefaults(void){
    ED.dmrEnabled    = false;
    ED.dmrTalkGroup  = 9;
    ED.dmrSrcId      = 0;
    ED.dmrTimeSlot   = 1;
    ED.dmrColorCode  = 1;
    SyncDmrConfigFromED();
    SaveDataToStorage(false);
}

struct SecondaryMenuOption DMROptions[] = {
    "Enable",         variableOption, &dmrEnableVar,    NULL, (void *)DmrMenuApplyAndSave,
    "Talk Group",     variableOption, &dmrTalkGroupVar, NULL, (void *)DmrMenuApplyAndSave,
    "My DMR ID",      variableOption, &dmrSrcIdVar,     NULL, (void *)DmrMenuApplyAndSave,
    "Time Slot",      variableOption, &dmrTimeSlotVar,  NULL, (void *)DmrMenuApplyAndSave,
    "Color Code",     variableOption, &dmrColorCodeVar, NULL, (void *)DmrMenuApplyAndSave,
    "Save Now",       functionOption, NULL, (void *)DmrMenuSaveNow,        NULL,
    "Reset Defaults", functionOption, NULL, (void *)DmrMenuResetDefaults,  NULL,
};

// Primary menu structure
struct PrimaryMenuOption primaryMenu[11] = {
    "RF Options", RFSet, sizeof(RFSet)/sizeof(RFSet[0]),
    "CW Options", CWOptions, sizeof(CWOptions)/sizeof(CWOptions[0]),
    "Microphone", MicOptions, sizeof(MicOptions)/sizeof(MicOptions[0]),
    "Audio Options", AudioOptions, sizeof(AudioOptions)/sizeof(AudioOptions[0]),
    "Display", DisplayOptions, sizeof(DisplayOptions)/sizeof(DisplayOptions[0]),
    "EEPROM", EEPROMOptions, sizeof(EEPROMOptions)/sizeof(EEPROMOptions[0]),
    "Calibration", CalOptions, sizeof(CalOptions)/sizeof(CalOptions[0]),
    "Diagnostics", DiagnosticOptions, sizeof(DiagnosticOptions)/sizeof(DiagnosticOptions[0]),
    "FT8", FT8Options, sizeof(FT8Options)/sizeof(FT8Options[0]),
    "PSK31", PSK31Options, sizeof(PSK31Options)/sizeof(PSK31Options[0]),
    "DMR", DMROptions, sizeof(DMROptions)/sizeof(DMROptions[0]),
};

/**
 * Update menu variable pointers to reference current band-specific values.
 */
void UpdateArrayVariables(void){
    ssbPower.variable = &ED.powerOutSSB[ED.currentBand[ED.activeVFO]];
    cwPower.variable = &ED.powerOutCW[ED.currentBand[ED.activeVFO]];
    rxAtten.variable = &ED.RAtten[ED.currentBand[ED.activeVFO]];
    txAttenCW.variable = &ED.XAttenCW[ED.currentBand[ED.activeVFO]];
    //txAttenSSB.variable = &ED.XAttenSSB[ED.currentBand[ED.activeVFO]];
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

/**
 * Render the primary (category) menu labels on the display.
 *
 * @param foreground If true, renders in normal foreground mode with current
 *                   selection highlighted in green. If false, renders in
 *                   background mode with muted colors.
 *
 * Displays all primary menu category names in a vertical list with the
 * currently selected category highlighted. Also displays the git commit
 * hash at the bottom of the menu.
 */
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
        /* When HAS_PSRAM is undefined (no PSRAM installed), the FT8
         * subsystem is hard-disabled because its 720 KB EXTMEM slot
         * buffer can't be allocated. Paint that menu row red and skip
         * the normal highlight bookkeeping so it's visually obvious. */
        bool ft8Disabled = false;
#ifndef HAS_PSRAM
        if (strcmp(primaryMenu[k].label, "FT8") == 0) ft8Disabled = true;
#endif
        if (ft8Disabled) {
            tft.setTextColor(RA8875_RED);
            tft.setCursor(x, y);
            tft.print(primaryMenu[k].label);
            /* Restore the page-default colour for whatever entries follow. */
            if (foreground) tft.setTextColor(RA8875_WHITE);
            else            tft.setTextColor(DARKGREY, RA8875_BLACK);
            y += delta;
            continue;
        }

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

/**
 * Render the secondary (option) menu labels for the current category.
 *
 * @param foreground If true, renders in normal foreground mode with current
 *                   selection highlighted in green. If false, renders in
 *                   background mode with muted colors.
 *
 * Displays all secondary menu option names for the currently selected
 * primary category in a vertical list with the currently selected
 * option highlighted.
 */
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

/**
 * Draw the main menu display (MAIN_MENU state).
 *
 * This function renders the primary menu categories with the secondary menu
 * options visible but dimmed. It handles:
 * - Initial screen clearing when entering the menu state
 * - Band/VFO change detection to update band-specific variable pointers
 * - Drawing the yellow border around the menu area
 * - Rendering primary menu in foreground and secondary menu in background
 *
 * The function only executes when in the MAIN_MENU UI state and only redraws
 * when the redrawMenu flag is set.
 */
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

/**
 * Draw the secondary menu display (SECONDARY_MENU state).
 *
 * This function renders the secondary menu options with the primary menu
 * categories visible but dimmed. It handles:
 * - Initial screen clearing when entering the secondary menu state
 * - Drawing the yellow border around the menu area
 * - Rendering primary menu in background and secondary menu in foreground
 *
 * The function only executes when in the SECONDARY_MENU UI state and only
 * redraws when the redrawMenu flag is set.
 */
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
