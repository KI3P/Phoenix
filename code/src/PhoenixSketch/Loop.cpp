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
            if(++ED.currentBand[ED.activeVFO] > LAST_BAND)
                ED.currentBand[ED.activeVFO] = FIRST_BAND;
            ED.centerFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][0];
            ED.fineTuneFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][1];
            UpdateRFHardwareState();
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
            ED.centerFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][0];
            ED.fineTuneFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][1];
            UpdateRFHardwareState();
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
                // Change the band if we tune out of the current band
                ED.currentBand[ED.activeVFO] = GetBand(ED.centerFreq_Hz[ED.activeVFO]);
                UpdateRFHardwareState();
                Debug(String("Center tune = ") + String(ED.centerFreq_Hz[ED.activeVFO]));
                break;
            }
            case (iCENTERTUNE_DECREASE):{
                ED.centerFreq_Hz[ED.activeVFO] -= (int64_t)ED.freqIncrement;
                // Change the band if we tune out of the current band
                ED.currentBand[ED.activeVFO] = GetBand(ED.centerFreq_Hz[ED.activeVFO]);
                UpdateRFHardwareState();
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