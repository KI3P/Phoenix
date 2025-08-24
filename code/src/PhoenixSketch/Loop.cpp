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

#ifdef FAST_TUNE
// The fast tune parameters
static uint64_t MS_temp, FT_delay, FT_last_time;
static bool FT_ON = false;
const uint64_t FT_cancel_ms = 500;  // time between steps above which FT is cancelled
const uint64_t FT_on_ms = 100;      // time between FTsteps below which increases the step size
const int32_t FT_trig = 4;          // number of short steps to trigger fast tune,
const int FT_step = 1000;           // Hz step in Fast Tune
static int64_t last_FT_step_size = 1;
static uint32_t FT_step_counter = 0;
#endif

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
    switch (bands[EEPROMData.currentBand].mode) {
        case LSB:{
            if (switchFilterSideband == 0)  // "0" = normal, "1" means change opposite filter
            {
                bands[EEPROMData.currentBand].FLoCut_Hz = 
                  bands[EEPROMData.currentBand].FLoCut_Hz - filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            } else {
                bands[EEPROMData.currentBand].FHiCut_Hz = 
                  bands[EEPROMData.currentBand].FHiCut_Hz - filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            }
            break;
        }
        case USB:{
            if (switchFilterSideband == 0) {
                bands[EEPROMData.currentBand].FHiCut_Hz = 
                  bands[EEPROMData.currentBand].FHiCut_Hz + filter_change * (int32_t)(40.0 * ENCODER_FACTOR);

            } else {
                bands[EEPROMData.currentBand].FLoCut_Hz = 
                  bands[EEPROMData.currentBand].FLoCut_Hz + filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            }
            break;
        }
        case AM:
        case SAM:{
            bands[EEPROMData.currentBand].FHiCut_Hz = 
              bands[EEPROMData.currentBand].FHiCut_Hz + filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            bands[EEPROMData.currentBand].FLoCut_Hz = -bands[EEPROMData.currentBand].FHiCut_Hz;
            break;
        }
        case IQ:
        case DCF77:
            break;
    }
    // Calculate the new FIR filter mask
    UpdateFIRFilterMask(&filters);
}

/**
 * Adjust the fine tune frequency (2nd stage software mixer)
 * 
 * @param filter_change The positive or negative increment to the filter setting
 */
void AdjustFineTune(int32_t filter_change){
    #ifdef FAST_TUNE
    MS_temp = millis();
    FT_delay = MS_temp - FT_last_time;
    FT_last_time = MS_temp;
    if (FT_ON) {  // Check if FT should be cancelled (FT_delay>=FT_cancel_ms)
        if (FT_delay >= FT_cancel_ms) {
            FT_ON = false;
            EEPROMData.stepFineTune = last_FT_step_size;
        }
    } else {  //  FT is off so check for short delays
        if (FT_delay <= FT_on_ms) {
            FT_step_counter += 1;
        }
        if (FT_step_counter >= FT_trig) {
            last_FT_step_size = EEPROMData.stepFineTune;
            EEPROMData.stepFineTune = FT_step;
            FT_step_counter = 0;
            FT_ON = true;
        }
    }
    #endif  // FAST_TUNE

    EEPROMData.fineTuneFreq_Hz += EEPROMData.stepFineTune * filter_change;

    // If the zoom level is 0, then the valid range of fine tune window is between
    // -samplerate/2 and +samplerate/2. 
    int32_t lower_limit = -(int32_t)(SR[SampleRate].rate)/2;
    int32_t upper_limit = (int32_t)(SR[SampleRate].rate)/2;
    if (EEPROMData.spectrum_zoom != 0) {
        // The fine tune frequency must stay within the visible tuning window, which
        // is determined by the zoom level. The visible window is determined by shifting
        // the spectrum by +48 kHz so the center is at EEPROMData.centerFreq_Hz - 48 kHz, 
        // then zooming in around the new center frequency.
        uint32_t visible_bandwidth = SR[SampleRate].rate / (1 << EEPROMData.spectrum_zoom);
        lower_limit = -(int32_t)visible_bandwidth/2;
        upper_limit = +(int32_t)visible_bandwidth/2;
    }
    if (EEPROMData.fineTuneFreq_Hz > upper_limit) EEPROMData.fineTuneFreq_Hz = upper_limit;
    if (EEPROMData.fineTuneFreq_Hz < lower_limit) EEPROMData.fineTuneFreq_Hz = lower_limit;

    // The fine tune is applied after the spectrum is shifted by samplerate/4. So the 
    // actual frequency in the RF domain is: TXRXFreq = centerFreq+fineTuneFreq-48kHz
    EEPROMData.currentFreqA = EEPROMData.centerFreq_Hz+EEPROMData.fineTuneFreq_Hz-(int32_t)SR[SampleRate].rate/4;
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
            EEPROMData.audioVolume++;
            if (EEPROMData.audioVolume > 100) EEPROMData.audioVolume = 100;
            sprintf(strbuf,"Volume: %ld",EEPROMData.audioVolume);
            Debug(strbuf);
            break;
        }
        case (iVOLUME_DECREASE):{
            EEPROMData.audioVolume--;
            if (EEPROMData.audioVolume < 0) EEPROMData.audioVolume = 0;
            sprintf(strbuf,"Volume: %ld",EEPROMData.audioVolume);
            Debug(strbuf);
            break;
        }
        case (iFILTER_INCREASE):{
            FilterSetSSB(1);
            Debug("Filter increase");
            break;
        }
        case (iFILTER_DECREASE):{
            FilterSetSSB(-1);
            Debug("Filter decrease");
            break;
        }
        case (iCENTERTUNE_INCREASE):{
            EEPROMData.centerFreq_Hz += (int64_t)EEPROMData.freqIncrement;
            SetFreq(EEPROMData.centerFreq_Hz);
            Debug("Center tune increase");
            break;
        }
        case (iCENTERTUNE_DECREASE):{
            EEPROMData.centerFreq_Hz -= (int64_t)EEPROMData.freqIncrement;
            SetFreq(EEPROMData.centerFreq_Hz);
            Debug("Center tune decrease");
            break;
        }
        case (iFINETUNE_INCREASE):{
            AdjustFineTune(+1);
            Debug("Fine tune increase");
            break;
        }
        case (iFINETUNE_DECREASE):{
            AdjustFineTune(-1);
            Debug("Fine tune decrease");
            break;
        }
        case (iBUTTON_PRESSED):{
            char strbuf[50];
            sprintf(strbuf, "Pressed button: %ld",GetButton());
            Debug(strbuf);
            break;
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

    // Tell the ATTiny that we have finished shutdown and it's safe to power off
    digitalWrite(SHUTDOWN_COMPLETE, 1);
}

FASTRUN void loop(void){
    // This is the loop that is executed again and again

    // Check for signal to begin shutdown and perform shutdown routine if requested
    if (digitalRead(BEGIN_TEENSY_SHUTDOWN) == HIGH) ShutdownTeensy();

    // Step 1: Check for new events and handle them
    CheckForFrontPanelInterrupts();
    ConsumeInterrupt();
    
    // Step 2: Perform signal processing
    PerformSignalProcessing();

    // Step 3: Draw the display
    DrawDisplay();

}