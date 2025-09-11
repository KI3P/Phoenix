#include "SDT.h"

// We should have an interrupt timer that ticks every 1 ms.
// Every time it ticks we pass a Do event to the state machines.

/* We have interrupts attached to the various buttons. They set event
 * flags that the loop polls for and acts upon. Two types of actions happen.
 * First, we might act immediately on the input.
 * Second, we issue an Event to the state machines.
 */

/*
SSB Receive State

Button          Response
MODE            Issue ModeSm_EventId_MODE
PTT_PRESSED     Issue ModeSm_EventId_PTT_PRESSED
FREQ_INC        Update frequency increment value
FREQ_ENC_INC    Increase the tune frequency by increment
FREQ_ENC_DEC    Decrease the tune frequency by increment
*/

static InterruptType interrupt = iNONE; /** The internal InterruptType variable */
static KeyTypeId keyType = KEYER_TYPE;
static bool keyerFlip = KEYER_FLIP;
static uint8_t switchFilterSideband = 0;
char strbuf[100];

/**
 * Sets the key type.
 * 
 * @param key The KeyTypeId to set the key to. Valid choices are KeyTypeId_Straight or KeyTypeId_Keyer
 */
void SetKeyType(KeyTypeId key){
    keyType = key;
}

/**
 * Sets the keyer logic so that KEY1 = dah and KEY2 = dit
 */
void SetKey1Dah(void){
    keyerFlip = true;
}

/**
 * Sets the keyer logic so that KEY1 = dit and KEY2 = dah
 */
void SetKey1Dit(void){
    keyerFlip = false;
}

/**
 * Get the value of the internal interrupt variable.
 * 
 * @return The value of the internal InterruptType variable.
 */
InterruptType GetInterrupt(void){
    return interrupt;
}

/**
 * Sets the internal interrupt variable.
 * 
 * @param i The InterruptType value to set it to.
 */
void SetInterrupt(InterruptType i){
    interrupt = i;
}

/**
 * Called every 1 milliseconds by the system timer. It dispatches a DO event to the 
 * state machines.
 */
void TimerInterrupt(void){
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    UISm_dispatch_event(&uiSM, UISm_EventId_DO);
}

/**
 * Change the FIR filter settings.
 * @param filter_change The positive or negative increment to the filter bandwidth
 */
void FilterSetSSB(int32_t filter_change) {
    // Change the band parameters
    switch (bands[ED.currentBand[ED.activeVFO]].mode) {
        case LSB:{
            if (switchFilterSideband == 0)  // "0" = normal, "1" means change opposite filter
            {
                bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz = 
                  bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz - filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            } else {
                bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz = 
                  bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz - filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            }
            break;
        }
        case USB:{
            if (switchFilterSideband == 0) {
                bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz = 
                  bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz + filter_change * (int32_t)(40.0 * ENCODER_FACTOR);

            } else {
                bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz = 
                  bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz + filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            }
            break;
        }
        case AM:
        case SAM:{
            bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz = 
              bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz + filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz = -bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
            break;
        }
        case IQ:
        case DCF77:
            break;
    }
    // Calculate the new FIR filter mask
    UpdateFIRFilterMask(&filters);
}

void HandleButtonPress(int32_t button){
    switch (button){
        case MENU_OPTION_SELECT:{
            break;
        }
        case MAIN_MENU_UP:{
            break;
        }
        case BAND_UP:{
            ED.lastFrequencies[ED.currentBand[ED.activeVFO]][0] = ED.centerFreq_Hz[ED.activeVFO];
            ED.lastFrequencies[ED.currentBand[ED.activeVFO]][1] = ED.fineTuneFreq_Hz[ED.activeVFO];
            if(++ED.currentBand[ED.activeVFO] > LAST_BAND)
                ED.currentBand[ED.activeVFO] = FIRST_BAND;
            ChangeBand();
            break;
        }
        case ZOOM:{
            break;
        }
        case NOISE_FLOOR:{
            break;
        }
        case BAND_DN:{
            ED.lastFrequencies[ED.currentBand[ED.activeVFO]][0] = ED.centerFreq_Hz[ED.activeVFO];
            ED.lastFrequencies[ED.currentBand[ED.activeVFO]][1] = ED.fineTuneFreq_Hz[ED.activeVFO];
            if(--ED.currentBand[ED.activeVFO] < FIRST_BAND)
                ED.currentBand[ED.activeVFO] = LAST_BAND;
            ChangeBand();
            break;
        }
        case SET_MODE:{
            break;
        }
        case DEMODULATION:{
            break;
        }
        case MAIN_TUNE_INCREMENT:{
            break;
        }
        case NOISE_REDUCTION:{
            break;
        }
        case NOTCH_FILTER:{
            break;
        }
        case FINE_TUNE_INCREMENT:{
            break;
        }
        case FILTER:{
            break;
        }
        case DECODER_TOGGLE:{
            break;
        }
        case DDE:{
            break;
        }
        case BEARING:{
            break;
        }
        default:
            break;
    }
}

/**
 * Considers the value of the interrupt and acts accordingly by either issueing an
 * event to the state machines or by updating a system parameter. Interrupt is 
 * consumed and interrupt variable set to NONE.
 */
void ConsumeInterrupt(void){
    switch (interrupt){
        case (iNONE):{
            break;
        }
        case (iPTT_PRESSED):{
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
            break;
        }
        case (iPTT_RELEASED):{
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
            break;
        }
        case (iMODE):{
            break;
        }
        case (iKEY1_PRESSED):{
            if (keyType == KeyTypeId_Straight){
                ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_PRESSED);
            } else {
                if (keyerFlip){
                    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DAH_PRESSED);
                }else{
                    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
                }
            }
            break;
        }
        case (iKEY2_PRESSED):{
            if (keyerFlip){
                ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
            }else{
                ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DAH_PRESSED);
            }
            break;
        }
        case (iVOLUME_INCREASE):{
            ED.audioVolume++;
            if (ED.audioVolume > 100) ED.audioVolume = 100;
            sprintf(strbuf,"Volume: %ld",ED.audioVolume);
            Debug(strbuf);
            break;
        }
        case (iVOLUME_DECREASE):{
            ED.audioVolume--;
            if (ED.audioVolume < 0) ED.audioVolume = 0;
            sprintf(strbuf,"Volume: %ld",ED.audioVolume);
            Debug(strbuf);
            break;
        }
        case (iFILTER_INCREASE):{
            FilterSetSSB(1);
            Debug(String("Filter = ") + String(bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz) 
                     + String(" to ") + String(bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz) );            break;
        }
        case (iFILTER_DECREASE):{
            FilterSetSSB(-1);
            Debug(String("Filter = ") + String(bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz) 
                     + String(" to ") + String(bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz) );
            break;
        }
        case (iCENTERTUNE_INCREASE):{
            ED.centerFreq_Hz[ED.activeVFO] += (int64_t)ED.freqIncrement;
            SetFreq(ED.centerFreq_Hz[ED.activeVFO]);
            Debug(String("Center tune = ") + String(ED.centerFreq_Hz[ED.activeVFO]));
            break;
        }
        case (iCENTERTUNE_DECREASE):{
            ED.centerFreq_Hz[ED.activeVFO] -= (int64_t)ED.freqIncrement;
            SetFreq(ED.centerFreq_Hz[ED.activeVFO]);
            Debug(String("Center tune = ") + String(ED.centerFreq_Hz[ED.activeVFO]));
            break;
        }
        case (iFINETUNE_INCREASE):{
            AdjustFineTune(+1);
            Debug(String("Fine tune = ") + String(ED.fineTuneFreq_Hz[ED.activeVFO]));
            break;
        }
        case (iFINETUNE_DECREASE):{
            AdjustFineTune(-1);
            Debug(String("Fine tune = ") + String(ED.fineTuneFreq_Hz[ED.activeVFO]));
            break;
        }
        case (iBUTTON_PRESSED):{
            Debug(String("Pressed button:") + String(GetButton()));
            HandleButtonPress(GetButton());
            break;
        }
        case (iVFO_CHANGE):{
            // The VFO has been updated. We might have selected a different active VFO,
            // we might have changed frequency.
            ChangeVFO();
            break;
        }
        case (iRECEIVE_TUNE):{
            ReceiveTune();
        }
        default:
            break;
    }
    // Clear the interrupt. If there was no interrupt this has no effect. If there was,
    // this resets the state so we don't try to handle it again.
    interrupt = iNONE;
}

/**
 * Shut down the radio gracefully when informed by the Shutdown circuitry 
 * that the power button has been pressed.
 */
void ShutdownTeensy(void)
{
    // Do whatever is needed before cutting power here
    // ... nothing yet

    // Tell the ATTiny that we have finished shutdown and it's safe to power off
    digitalWrite(SHUTDOWN_COMPLETE, 1);
}

FASTRUN void loop(void){
    // This is the loop that is executed again and again

    // Check for signal to begin shutdown and perform shutdown routine if requested
    if (digitalRead(BEGIN_TEENSY_SHUTDOWN) == HIGH) ShutdownTeensy();

    // Step 1: Check for new events and handle them
    CheckForFrontPanelInterrupts();
    CheckForCATSerialEvents();
    ConsumeInterrupt();
    
    // Step 2: Perform signal processing
    PerformSignalProcessing();

    // Step 3: Draw the display
    DrawDisplay();

}