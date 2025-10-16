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

// FIFO buffer for interrupt events
#define INTERRUPT_BUFFER_SIZE 16
static struct {
    InterruptType buffer[INTERRUPT_BUFFER_SIZE];
    volatile size_t head;  // Points to next position to write
    volatile size_t tail;  // Points to next position to read
    volatile size_t count; // Number of items in buffer
} interruptFifo = {{iNONE}, 0, 0, 0};

static uint8_t switchFilterSideband = 0;
char strbuf[100];

/**
 * Sets the key type.
 * 
 * @param key The KeyTypeId to set the key to. Valid choices are KeyTypeId_Straight or KeyTypeId_Keyer
 */
void SetKeyType(KeyTypeId key){
    ED.keyType = key;
}

/**
 * Sets the keyer logic so that KEY1 = dah and KEY2 = dit
 */
void SetKey1Dah(void){
    ED.keyerFlip = true;
}

/**
 * Sets the keyer logic so that KEY1 = dit and KEY2 = dah
 */
void SetKey1Dit(void){
    ED.keyerFlip = false;
}

/**
 * Get the next interrupt from the FIFO buffer.
 *
 * @return The next InterruptType from the buffer, or iNONE if buffer is empty.
 */
InterruptType GetInterrupt(void){
    if (interruptFifo.count == 0) {
        return iNONE;
    }

    InterruptType result = interruptFifo.buffer[interruptFifo.tail];
    interruptFifo.tail = (interruptFifo.tail + 1) % INTERRUPT_BUFFER_SIZE;
    interruptFifo.count--;

    return result;
}

size_t GetInterruptFifoSize(void){
    return interruptFifo.count;
}

/**
 * Adds an interrupt to the end of the FIFO buffer.
 *
 * @param i The InterruptType value to add to the buffer.
 */
void SetInterrupt(InterruptType i){
    if (interruptFifo.count >= INTERRUPT_BUFFER_SIZE) {
        // Buffer is full, drop the oldest interrupt
        interruptFifo.tail = (interruptFifo.tail + 1) % INTERRUPT_BUFFER_SIZE;
        interruptFifo.count--;
    }

    interruptFifo.buffer[interruptFifo.head] = i;
    interruptFifo.head = (interruptFifo.head + 1) % INTERRUPT_BUFFER_SIZE;
    interruptFifo.count++;
}

/**
 * Adds an interrupt to the beginning of the FIFO buffer.
 *
 * @param i The InterruptType value to add to the buffer.
 */
void PrependInterrupt(InterruptType i){
    if (interruptFifo.count >= INTERRUPT_BUFFER_SIZE) {
        // Buffer is full, drop the oldest interrupt (at head-1)
        interruptFifo.head = (interruptFifo.head - 1 + INTERRUPT_BUFFER_SIZE) % INTERRUPT_BUFFER_SIZE;
        interruptFifo.count--;
    }

    // Move tail backward to insert at the beginning
    interruptFifo.tail = (interruptFifo.tail - 1 + INTERRUPT_BUFFER_SIZE) % INTERRUPT_BUFFER_SIZE;
    interruptFifo.buffer[interruptFifo.tail] = i;
    interruptFifo.count++;
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
            ED.lastFrequencies[ED.currentBand[ED.activeVFO]][2] = ED.modulation[ED.activeVFO];
            if(++ED.currentBand[ED.activeVFO] > LAST_BAND)
                ED.currentBand[ED.activeVFO] = FIRST_BAND;
            ED.centerFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][0];
            ED.fineTuneFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][1];
            ED.modulation[ED.activeVFO] = (ModulationType)ED.lastFrequencies[ED.currentBand[ED.activeVFO]][2];
            UpdateRFHardwareState();
            Debug("Band is " + String(bands[ED.currentBand[ED.activeVFO]].name));
            break;
        }
        case BAND_DN:{
            ED.lastFrequencies[ED.currentBand[ED.activeVFO]][0] = ED.centerFreq_Hz[ED.activeVFO];
            ED.lastFrequencies[ED.currentBand[ED.activeVFO]][1] = ED.fineTuneFreq_Hz[ED.activeVFO];
            ED.lastFrequencies[ED.currentBand[ED.activeVFO]][2] = ED.modulation[ED.activeVFO];
            if(--ED.currentBand[ED.activeVFO] < FIRST_BAND)
                ED.currentBand[ED.activeVFO] = LAST_BAND;
            ED.centerFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][0];
            ED.fineTuneFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][1];
            ED.modulation[ED.activeVFO] = (ModulationType)ED.lastFrequencies[ED.currentBand[ED.activeVFO]][2];
            UpdateRFHardwareState();
            Debug("Band is " + String(bands[ED.currentBand[ED.activeVFO]].name));
            break;
        }
        case ZOOM:{
            ED.spectrum_zoom++;
            if (ED.spectrum_zoom > SPECTRUM_ZOOM_MAX)
                ED.spectrum_zoom = SPECTRUM_ZOOM_MIN;
            Debug("Zoom is x" + String(1<<ED.spectrum_zoom));
            ZoomFFTPrep(ED.spectrum_zoom, &filters);
            break;
        }
        case RESET_TUNING:{
            ResetTuning();
            UpdateRFHardwareState();
            Debug("Center freq = " + String(ED.centerFreq_Hz[ED.activeVFO]));
            Debug("Fine tune freq = " + String(ED.fineTuneFreq_Hz[ED.activeVFO]));
            break;
        }
        case TOGGLE_MODE:{
            switch(modeSM.state_id){
                case ModeSm_StateId_SSB_RECEIVE:{
    				ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
                    UpdateRFHardwareState();
                    break;
                }
                case ModeSm_StateId_CW_RECEIVE:{
    				ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_SSB_MODE);
	    			UpdateRFHardwareState();
                    break;
                }
                default:{
                    break;
                }
            }
            Debug("Mode is " + String(modeSM.state_id));
            break;
        }
        case DEMODULATION:{
            // Rotate through the modulation types USB(0), LSB(1), AM(2), and SAM(3)
            int8_t newmod = (int8_t)ED.modulation[ED.activeVFO] + 1;
            if (newmod > (int8_t)SAM)
                newmod = (int8_t)USB;
            ED.modulation[ED.activeVFO] = (ModulationType)newmod;
            Debug("Modulation is " + String(ED.modulation[ED.activeVFO]));
            break;
        }
        case MAIN_TUNE_INCREMENT:{
            int32_t incrementValues[] = { 10, 50, 100, 250, 1000, 10000, 100000, 1000000 };
            // find the index of the current increment
            size_t i = 0;
            for (i = 0; i < sizeof(incrementValues)/sizeof(int32_t); i++)
                if (incrementValues[i] == ED.freqIncrement) break;
            i++; // increase it
            if (i >= sizeof(incrementValues)/sizeof(int32_t)) // check for end of array
                i = 0;
            ED.freqIncrement = incrementValues[i];
            Debug("Main tune increment is " + String(ED.freqIncrement));
            break;
        }
        case FINE_TUNE_INCREMENT:{
            int32_t selectFT[] = { 10, 50, 250, 500 };
            size_t i = 0;
            for (i = 0; i < sizeof(selectFT)/sizeof(int32_t); i++)
                if (ED.stepFineTune == selectFT[i]) break;
            i++;
            if (i >= sizeof(selectFT)/sizeof(int32_t))
                i = 0;
            ED.stepFineTune = selectFT[i];
            Debug("Fine tune increment is " + String(ED.stepFineTune));
            break;
        }
        case NOISE_REDUCTION:{
            // Rotate through the noise reduction types
            int8_t newnr = (int8_t)ED.nrOptionSelect + 1;
            if (newnr > (int8_t)NRLMS)
                newnr = (int8_t)NROff;
            ED.nrOptionSelect = (NoiseReductionType)newnr;
            Debug("Noise reduction is " + String(ED.nrOptionSelect));
            break;
        }
        case NOTCH_FILTER:{
            if (ED.ANR_notchOn == 0)
                ED.ANR_notchOn = 1;
            else
                ED.ANR_notchOn = 0;
            Debug("Notch filter is " + String(ED.ANR_notchOn));
            break;
        }
        case FILTER:{
            // I am not sure what the point of this button is, so ignore for now
            break;
        }
        case DECODER_TOGGLE:{
            if (ED.decoderFlag == 0)
                ED.decoderFlag = 1;
            else
                ED.decoderFlag = 0;
            Debug("Decoder is " + String(ED.decoderFlag));
            break;
        }
        case DDE:{
            // Add this later
            break;
        }
        case BEARING:{
            // Add this later
            break;
        }
        case VFOTOGGLE:{
            if (ED.activeVFO == 0)
                ED.activeVFO = 1;
            else
                ED.activeVFO = 0;
            UpdateRFHardwareState();
            break;
        }
        case VOLUMEBUTTON:{
            // Rotate through the parameters controlled by the volume knob
            int8_t newvol = (int8_t)volumeFunction + 1;
            if (newvol > (int8_t)SidetoneVolume)
                newvol = (int8_t)AudioVolume;
            volumeFunction = (VolumeFunction)newvol;
            Debug("Volume knob function is " + String(volumeFunction));
            break;
        }
        case FINETUNEBUTTON:{
            break;
        }
        default:
            break;
    }
}

/**
 * The keyer requires special handling logic because of its timers
 */
void HandleKeyer(InterruptType interrupt){
    if ((interrupt != iKEY1_PRESSED) && (interrupt != iKEY2_PRESSED))
        return; // this should never happen

    // act on this interrupt if we're in the CW_RECEIVE or CW_TRANSMIT_KEYER_WAIT states
    // If we're in the TRANSMIT_DIT_MARK, TRANSMIT_DAH_MARK, or TRANSMIT_KEYER_SPACE states,
    // add it back to the head of the interrupt queue. If we're in any other state, discard
    // it without acting on it
    switch (modeSM.state_id){
        case ModeSm_StateId_CW_RECEIVE:
        case ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT:{
            switch (interrupt){
                case (iKEY1_PRESSED):{
                    if (ED.keyerFlip){
                        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DAH_PRESSED);
                    }else{
                        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
                    }
                    break;
                }
                case (iKEY2_PRESSED):{
                    if (ED.keyerFlip){
                        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
                    }else{
                        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DAH_PRESSED);
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case ModeSm_StateId_CW_TRANSMIT_DAH_MARK:
        case ModeSm_StateId_CW_TRANSMIT_DIT_MARK:
        case ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE:{
            PrependInterrupt(interrupt);
            break;
        }
        default:
            break;
    }
}

/**
 * Considers the next interrupt from the FIFO buffer and acts accordingly by either 
 * issuing an event to the state machines or by updating a system parameter. Interrupt 
 * is consumed and removed from the buffer.
 */
void ConsumeInterrupt(void){
    InterruptType interrupt = GetInterrupt();
    if ( interrupt != iNONE ){
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
                // mode has changed, recalc filters, change frequencies, etc
                UpdateRFHardwareState();
                break;
            }
            case (iKEY1_PRESSED):{
                if (ED.keyType == KeyTypeId_Straight){
                    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_PRESSED);
                } else {
                    HandleKeyer(interrupt);
                }
                break;
            }
            case (iKEY1_RELEASED):{
                if (ED.keyType == KeyTypeId_Straight){
                    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_RELEASED);
                }
                break;
            }
            case (iKEY2_PRESSED):{
                HandleKeyer(interrupt);
                break;
            }
            case (iVOLUME_INCREASE):{
                // Triggered by the encoder turning. Update depending on what
                // the state of the volumeFunction variable is. This variable is
                // changed by pressing the button on the volume encoder
                switch (volumeFunction) {
                    case AudioVolume:{
                        ED.audioVolume++;
                        if (ED.audioVolume > 100) ED.audioVolume = 100;
                        break;
                    }
                    case AGCGain:{
                        bands[ED.currentBand[ED.activeVFO]].AGC_thresh++;
                        break;
                    }
                    case MicGain:{
                        ED.currentMicGain++;
                        break;
                    }
                    case SidetoneVolume:{
                        ED.sidetoneVolume += 1.0;
                        if (ED.sidetoneVolume > 500) ED.sidetoneVolume = 500; // 0 to 500 range
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case (iVOLUME_DECREASE):{
                switch (volumeFunction) {
                    case AudioVolume:{
                        ED.audioVolume--;
                        if (ED.audioVolume < 0) ED.audioVolume = 0;
                        break;
                    }
                    case AGCGain:{
                        bands[ED.currentBand[ED.activeVFO]].AGC_thresh--;
                        break;
                    }
                    case MicGain:{
                        ED.currentMicGain--;
                        // peg to zero if it goes too low, unsigned int expected
                        if (ED.currentMicGain < 0) ED.currentMicGain = 0;
                        break;
                    }
                    case SidetoneVolume:{
                        ED.sidetoneVolume -= 1.0;
                        if (ED.sidetoneVolume < 0) ED.sidetoneVolume = 0; // 0 to 500 range
                        break;
                    }
                    default:
                        break;
                }
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
                // Change the band if we tune out of the current band
                ED.currentBand[ED.activeVFO] = GetBand(GetTXRXFreq(ED.activeVFO));
                UpdateRFHardwareState();
                //Debug(String("Center tune = ") + String(ED.centerFreq_Hz[ED.activeVFO]));
                break;
            }
            case (iCENTERTUNE_DECREASE):{
                ED.centerFreq_Hz[ED.activeVFO] -= (int64_t)ED.freqIncrement;
                if (ED.centerFreq_Hz[ED.activeVFO] < 250000)
                    ED.centerFreq_Hz[ED.activeVFO] = 250000;
                // Change the band if we tune out of the current band
                ED.currentBand[ED.activeVFO] = GetBand(GetTXRXFreq(ED.activeVFO));
                UpdateRFHardwareState();
                //Debug(String("Center tune = ") + String(ED.centerFreq_Hz[ED.activeVFO]));
                break;
            }
            case (iFINETUNE_INCREASE):{
                AdjustFineTune(+1);
                ED.currentBand[ED.activeVFO] = GetBand(GetTXRXFreq(ED.activeVFO));
                //Debug(String("Fine tune = ") + String(ED.fineTuneFreq_Hz[ED.activeVFO]));
                break;
            }
            case (iFINETUNE_DECREASE):{
                AdjustFineTune(-1);
                ED.currentBand[ED.activeVFO] = GetBand(GetTXRXFreq(ED.activeVFO));
                //Debug(String("Fine tune = ") + String(ED.fineTuneFreq_Hz[ED.activeVFO]));
                break;
            }
            case (iBUTTON_PRESSED):{
                int32_t button = GetButton();
                Debug(String("Pressed button:") + String(button));
                HandleButtonPress(button);
                break;
            }
            case (iVFO_CHANGE):{
                // The VFO has been updated. We might have selected a different active VFO,
                // we might have changed frequency.
                if (ED.activeVFO == 0){
                    ED.activeVFO = 1;
                }else{
                    ED.activeVFO = 0;
                }
                UpdateRFHardwareState();
                break;
            }
            case (iUPDATE_TUNE):{
                UpdateRFHardwareState();
                break;
            }
            case (iPOWER_CHANGE):{
                // Nothing here yet
                break;
            }
            default:
                break;
        }
    }
}

/**
 * Shut down the radio gracefully when informed by the Shutdown circuitry 
 * that the power button has been pressed.
 */
void ShutdownTeensy(void){
    // Do whatever is needed before cutting power here
    SaveDataToStorage();
    
    // Tell the ATTiny that we have finished shutdown and it's safe to power off
    digitalWrite(SHUTDOWN_COMPLETE, 1);
    MyDelay(1000); // wait for the turn off command
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