/**
 * @file Loop.cpp
 * @brief Main program loop implementation for Phoenix SDR Radio
 *
 * ARCHITECTURE OVERVIEW
 * =====================
 * The Phoenix SDR is a Teensy 4.1-based amateur radio transceiver that uses a
 * state machine architecture for deterministic hardware control and real-time
 * digital signal processing.
 *
 * Core Architectural Principles:
 * ------------------------------
 * 1. STATE MACHINE CONTROL
 *    - Hardware state changes are managed by StateSmith-generated state machines
 *    - ModeSm: Controls radio operating mode (SSB_RECEIVE, SSB_TRANSMIT, CW modes)
 *    - UISm: Manages user interface states (HOME, MAIN_MENU, SECONDARY_MENU, UPDATE)
 *    - Tune state machine: Manages VFO frequency control for RX/TX operations
 *    - All state transitions are event-driven and deterministic
 *
 * 2. EVENT-DRIVEN ARCHITECTURE
 *    - Hardware interrupts (buttons, encoders, CW keys) are queued in a FIFO buffer
 *    - Main loop processes events sequentially from the interrupt buffer
 *    - Events trigger state machine transitions via dispatch_event() calls
 *    - Timer interrupts dispatch periodic DO events to state machines
 *
 * 3. REAL-TIME CONSTRAINTS
 *    - Main loop must complete within ~10ms to prevent audio buffer overflow
 *    - DSP processing is optimized and uses FASTRUN annotation for critical paths
 *    - Interrupt handlers are kept minimal, deferring work to main loop
 *
 * ROLE OF Loop.cpp
 * ================
 * This file implements the central event processing and main loop execution.
 * It serves as the "glue" between hardware events and state machine control.
 *
 * Key Responsibilities:
 * ---------------------
 * 1. INTERRUPT BUFFER MANAGEMENT
 *    - Maintains a FIFO queue for hardware interrupt events (iNONE, iPTT_PRESSED, etc.)
 *    - Provides thread-safe SetInterrupt() for ISRs to queue events
 *    - ConsumeInterrupt() processes events and dispatches to state machines
 *
 * 2. CW KEY HANDLING
 *    - Debounces CW key inputs (KEY1, KEY2)
 *    - Supports both straight key and iambic keyer operation
 *    - Routes key events to ModeSm for transmit control
 *
 * 3. USER INPUT PROCESSING
 *    - Processes encoder rotations (tuning, volume, filter adjustment)
 *    - Handles button presses (band change, mode toggle, menu navigation)
 *    - Routes UI events to UISm for menu system control
 *
 * 4. MAIN LOOP EXECUTION (loop() function)
 *    - Polls for hardware interrupts from front panel and CAT interface
 *    - Processes debouncing for mechanical contacts
 *    - Consumes interrupt events and dispatches to appropriate handlers
 *    - Performs real-time DSP processing via PerformSignalProcessing()
 *    - Updates display via DrawDisplay()
 *    - Monitors for shutdown signal and performs graceful shutdown
 *
 * Main Loop Execution Flow:
 * --------------------------
 *   1. Check for shutdown signal
 *   2. Poll and debounce CW key inputs
 *   3. Check front panel for button/encoder events
 *   4. Check CAT serial interface for commands
 *   5. Process next event from interrupt FIFO
 *   6. Perform DSP processing on audio buffers
 *   7. Update display with current radio state
 *   8. Loop repeats (target < 10ms per iteration)
 *
 * Integration with Other Modules:
 * --------------------------------
 * - ModeSm/UISm: Receives dispatched events from interrupt processing
 * - RFBoard/Tune: Updated via UpdateRFHardwareState() when frequency changes
 * - DSP modules: Called via PerformSignalProcessing() for audio processing
 * - FrontPanel: Polled for button/encoder events via CheckForFrontPanelInterrupts()
 * - CAT: Polled for serial commands via CheckForCATSerialEvents()
 * - Storage: Called during shutdown to save radio state
 *
 * @see ModeSm.cpp for radio mode state machine implementation
 * @see UISm.cpp for user interface state machine implementation
 * @see Tune.cpp for VFO control state machine implementation
 * @see DSP.cpp for signal processing implementation
 */

#include "SDT.h"

// FIFO buffer for interrupt events
#define INTERRUPT_BUFFER_SIZE 16
static struct {
    InterruptType buffer[INTERRUPT_BUFFER_SIZE];
    volatile size_t head;  // Points to next position to write
    volatile size_t tail;  // Points to next position to read
    volatile size_t count; // Number of items in buffer
} interruptFifo = {{iNONE}, 0, 0, 0};

static uint8_t changeFilterHiCut = 0;
char strbuf[100];

///////////////////////////////////////////////////////////////////////////////
// CW key section
///////////////////////////////////////////////////////////////////////////////

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

const uint32_t DEBOUNCE_DELAY = 50; // 50ms debounce time
static bool lastKey1State = HIGH; // starts high due to input pullup
static uint32_t lastKey1ChangeTime = 0;
static volatile bool key1PendingRead = false;

/**
 * Interrupt service routine for KEY1 state changes (both rising and falling edges).
 *
 * This fast interrupt handler runs in RAM (FASTRUN) to minimize latency. It does not
 * directly read the pin state to avoid bounce issues. Instead, it records the time of
 * the edge change and sets a flag for the main loop to process after debounce delay.
 *
 * @see ProcessKey1Debounce() for debounce processing in main loop
 */
FASTRUN void Key1Change(void){
    // On ANY edge change, just note that something changed and restart the timer
    lastKey1ChangeTime = millis();
    key1PendingRead = true;
}

/**
 * Process Key1 debouncing by reading the actual pin state after the debounce
 * period has elapsed. This ensures the final stable state is always captured,
 * even if switch bouncing occurs during the transition.
 *
 * This should be called regularly from the main loop.
 */
void ProcessKey1Debounce(void){
    if (key1PendingRead) {
        uint32_t currentTime = millis();
        // Check if enough time has passed since last edge
        if (currentTime - lastKey1ChangeTime >= DEBOUNCE_DELAY) {
            // Now read the actual state - this is guaranteed to be stable
            bool currentState = digitalRead(KEY1);
            if (currentState != lastKey1State) {
                if (currentState) {
                    // Rising edge detected
                    SetInterrupt(iKEY1_RELEASED);
                } else {
                    // Falling edge detected
                    SetInterrupt(iKEY1_PRESSED);
                }
                lastKey1State = currentState;
            }
            key1PendingRead = false;
        }
    }
}

static uint32_t lastKey2time = 0;

/**
 * Interrupt service routine for KEY2 falling edge (key press).
 *
 * This fast interrupt handler runs in RAM (FASTRUN) to minimize latency. It performs
 * simple time-based debouncing by ignoring interrupts that occur within DEBOUNCE_DELAY
 * of the previous interrupt. Valid key presses are queued to the interrupt FIFO.
 *
 * Only falling edges (key press) are handled; KEY2 releases are not monitored for
 * iambic keyer operation.
 */
FASTRUN void Key2On(void){
    uint32_t currentTime = millis();
    // Check if enough time has passed since last interrupt
    if (currentTime - lastKey2time < DEBOUNCE_DELAY) {
        return; // Ignore this interrupt (likely bounce)
    }
    SetInterrupt(iKEY2_PRESSED);
    lastKey2time = currentTime;
}

static bool lastPTTState = HIGH; // starts high due to input pullup
static uint32_t lastPTTChangeTime = 0;
static volatile bool PTTPendingRead = false;

/**
 * Interrupt service routine for PTT state changes (both rising and falling edges).
 *
 * This fast interrupt handler runs in RAM (FASTRUN) to minimize latency. It does not
 * directly read the pin state to avoid bounce issues. Instead, it records the time of
 * the edge change and sets a flag for the main loop to process after debounce delay.
 *
 * @see ProcessPTTDebounce() for debounce processing in main loop
 */
FASTRUN void PTTChange(void){
    // On ANY edge change, just note that something changed and restart the timer
    lastPTTChangeTime = millis();
    PTTPendingRead = true;
}

/**
 * Process PTT debouncing by reading the actual pin state after the debounce
 * period has elapsed. This ensures the final stable state is always captured,
 * even if switch bouncing occurs during the transition.
 *
 * This should be called regularly from the main loop.
 */
void ProcessPTTDebounce(void){
    if (PTTPendingRead) {
        uint32_t currentTime = millis();
        // Check if enough time has passed since last edge
        if (currentTime - lastPTTChangeTime >= DEBOUNCE_DELAY) {
            // Now read the actual state - this is guaranteed to be stable
            bool currentState = digitalRead(PTT);
            if (currentState != lastPTTState) {
                if (currentState) {
                    // Rising edge detected
                    SetInterrupt(iPTT_RELEASED);
                } else {
                    // Falling edge detected
                    SetInterrupt(iPTT_PRESSED);
                }
                lastPTTState = currentState;
            }
            PTTPendingRead = false;
        }
    }
}

/**
 * Configure GPIO pins and attach interrupt handlers for CW key inputs.
 *
 * Sets up KEY1 and KEY2 pins with internal pull-up resistors (keys ground the inputs
 * when pressed). Attaches interrupt handlers:
 * - KEY1: Triggers on CHANGE (both edges) for debounce processing in main loop
 * - KEY2: Triggers on FALLING edge for iambic keyer second paddle
 *
 * Must be called during initialization before entering main loop.
 */
void SetupCWKeyInterrupts(void){
    // Set up interrupts for key
    pinMode(KEY1, INPUT_PULLUP);
    pinMode(KEY2, INPUT_PULLUP);
    pinMode(PTT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(KEY1), Key1Change, CHANGE);
    attachInterrupt(digitalPinToInterrupt(KEY2), Key2On, FALLING);
    attachInterrupt(digitalPinToInterrupt(PTT), PTTChange, CHANGE);
}

///////////////////////////////////////////////////////////////////////////////
// Interrupt buffer section
///////////////////////////////////////////////////////////////////////////////

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

/**
 * Get the current number of pending interrupts in the FIFO buffer.
 *
 * @return Number of interrupt events currently queued in the buffer (0-16).
 */
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

///////////////////////////////////////////////////////////////////////////////
// Code for handling button presses and state changes
///////////////////////////////////////////////////////////////////////////////
void ChangeRXIQIncrement(void); // forward declare from MainBoard_DisplayCalibration.cpp
void ChangeTXIQIncrement(void); // forward declare from MainBoard_DisplayCalibration.cpp
void ToggleRXTXEqualizerEdit(void); // from MainBoard_DisplayEqualizer.cpp
void AdjustEqualizerIncrement(void); // from MainBoard_DisplayEqualizer.cpp

/**
 * Process button press events from the front panel.
 *
 * Routes button presses to appropriate handlers based on button ID. Handles:
 * - Band selection (BAND_UP, BAND_DN)
 * - Mode toggles (TOGGLE_MODE, DEMODULATION)
 * - Tuning increment changes (MAIN_TUNE_INCREMENT, FINE_TUNE_INCREMENT)
 * - VFO control (VFO_TOGGLE, RESET_TUNING)
 * - DSP controls (NOISE_REDUCTION, NOTCH_FILTER, DECODER_TOGGLE)
 * - UI navigation (MENU_OPTION_SELECT, MAIN_MENU_UP, HOME_SCREEN)
 * - Display controls (ZOOM)
 * - Volume/filter encoder mode changes
 *
 * Some button handlers dispatch events to state machines (UISm, ModeSm), while
 * others directly modify system parameters and update hardware state.
 *
 * @param button Button ID from the front panel (defined in button constants)
 */
void HandleButtonPress(int32_t button){
    // Disable all buttons when in an active transmit mode
    if ((modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_DAH_MARK) ||
        (modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_DIT_MARK) ||
        (modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE) ||
        (modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT) ||
        (modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_MARK) ||
        (modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_SPACE) ||
        (modeSM.state_id == ModeSm_StateId_SSB_TRANSMIT))
        return;

    switch (uiSM.state_id){
        case (UISm_StateId_UPDATE):
        case (UISm_StateId_HOME):{
            switch (button){
                // You are in UISm_StateId_[HOME,UPDATE] states
                case MENU_OPTION_SELECT:{
                    // Issue SELECT interrupt to UI
                    UISm_dispatch_event(&uiSM,UISm_EventId_SELECT);
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case MAIN_MENU_UP:{
                    // Bring up the main menu
                    UISm_dispatch_event(&uiSM,UISm_EventId_MENU);
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case HOME_SCREEN:{
                    // Go back to the home screen
                    UISm_dispatch_event(&uiSM,UISm_EventId_HOME);
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
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
                // You are in UISm_StateId_[HOME,UPDATE] states
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
                // You are in UISm_StateId_[HOME,UPDATE] states
                case ZOOM:{
                    ED.spectrum_zoom++;
                    if (ED.spectrum_zoom > SPECTRUM_ZOOM_MAX)
                        ED.spectrum_zoom = SPECTRUM_ZOOM_MIN;
                    Debug("Zoom is x" + String(1<<ED.spectrum_zoom));
                    ZoomFFTPrep(ED.spectrum_zoom, &RXfilters);
                    ResetTuning();
                    UpdateRFHardwareState();
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case RESET_TUNING:{
                    ResetTuning();
                    UpdateRFHardwareState();
                    Debug("Center freq = " + String(ED.centerFreq_Hz[ED.activeVFO]));
                    Debug("Fine tune freq = " + String(ED.fineTuneFreq_Hz[ED.activeVFO]));
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
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
                // You are in UISm_StateId_[HOME,UPDATE] states
                case DEMODULATION:{
                    // Rotate through the modulation types USB(0), LSB(1), AM(2), and SAM(3)
                    int8_t newmod = (int8_t)ED.modulation[ED.activeVFO] + 1;
                    if (newmod > (int8_t)SAM)
                        newmod = (int8_t)USB;
                    ED.modulation[ED.activeVFO] = (ModulationType)newmod;
                    UpdateFIRFilterMask(&RXfilters);
                    Debug("Modulation is " + String(ED.modulation[ED.activeVFO]));
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
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
                // You are in UISm_StateId_[HOME,UPDATE] states
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
                // You are in UISm_StateId_[HOME,UPDATE] states
                case NOISE_REDUCTION:{
                    // Rotate through the noise reduction types
                    int8_t newnr = (int8_t)ED.nrOptionSelect + 1;
                    if (newnr > (int8_t)NRLMS)
                        newnr = (int8_t)NROff;
                    ED.nrOptionSelect = (NoiseReductionType)newnr;
                    Debug("Noise reduction is " + String(ED.nrOptionSelect));
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case NOTCH_FILTER:{
                    if (ED.ANR_notchOn == 0)
                        ED.ANR_notchOn = 1;
                    else
                        ED.ANR_notchOn = 0;
                    Debug("Notch filter is " + String(ED.ANR_notchOn));
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case FILTER:{
                    // I am not sure what the point of this button is, so ignore for now
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case DECODER_TOGGLE:{
                    if (ED.decoderFlag == 0)
                        ED.decoderFlag = 1;
                    else
                        ED.decoderFlag = 0;
                    Debug("Decoder is " + String(ED.decoderFlag));
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case DFE:{
                    // Go to direct frequency entry state
                    UISm_dispatch_event(&uiSM,UISm_EventId_DFE);
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case BEARING:{
                    // Add this later
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case VFO_TOGGLE:{
                    if (ED.activeVFO == 0)
                        ED.activeVFO = 1;
                    else
                        ED.activeVFO = 0;
                    UpdateRFHardwareState();
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case VOLUME_BUTTON:{
                    // Rotate through the parameters controlled by the volume knob
                    int8_t newvol = (int8_t)volumeFunction + 1;
                    if (newvol > (int8_t)SidetoneVolume)
                        newvol = (int8_t)AudioVolume;
                    volumeFunction = (VolumeFunction)newvol;
                    Debug("Volume knob function is " + String(volumeFunction));
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case FINETUNE_BUTTON:{
                    break;
                }
                // You are in UISm_StateId_[HOME,UPDATE] states
                case FILTER_BUTTON:{
                    if (changeFilterHiCut)
                        changeFilterHiCut = 0;
                    else
                        changeFilterHiCut = 1;
                    break;
                    Debug("changeFilterHiCut is " + String(changeFilterHiCut));
                }
                default:
                    break;
            }
            break;
        } // end of switch (button) for HOME and UPDATE states
        case (UISm_StateId_MAIN_MENU):
        case (UISm_StateId_SECONDARY_MENU):{
            switch(button){
                // You are in UISm_StateId_[MAIN,SECONDARY]_MENU states
                case MENU_OPTION_SELECT:{
                    // Issue SELECT interrupt to UI
                    UISm_dispatch_event(&uiSM,UISm_EventId_SELECT);
                    break;
                }
                // You are in UISm_StateId_[MAIN,SECONDARY]_MENU states
                case MAIN_MENU_UP:{
                    // Bring up the main menu
                    UISm_dispatch_event(&uiSM,UISm_EventId_MENU);
                    break;
                }
                // You are in UISm_StateId_[MAIN,SECONDARY]_MENU states
                case HOME_SCREEN:{
                    // Go back to the home screen
                    UISm_dispatch_event(&uiSM,UISm_EventId_HOME);
                    break;
                }
                default: // ignore all the other buttons
                    break;
            }
            break;
        } // end of MAIN and SECONDARY MENU states
        case (UISm_StateId_FREQ_ENTRY):{
            // Interpret buttons as number pad presses
            switch (button){
                case HOME_SCREEN:{
                    // Go back to the home screen without changing frequency
                    UISm_dispatch_event(&uiSM,UISm_EventId_HOME);
                    break;
                }
                default:
                    InterpretFrequencyEntryButtonPress(button);
                    break;
            }
            break;
        } // end of FREQ_ENTRY state
        case (UISm_StateId_EQUALIZER):{
            switch (button){
                case HOME_SCREEN:{
                    SaveDataToStorage(false);
                    UISm_dispatch_event(&uiSM,UISm_EventId_HOME);
                    break;
                }
                case 15:{
                    ToggleRXTXEqualizerEdit();
                    break;
                }
                case 16:{
                    AdjustEqualizerIncrement();
                    break;
                }
                default:
                    break;
            }
            break;
        } // end of FREQ_ENTRY state
        case (UISm_StateId_CALIBRATE_FREQUENCY):{
            switch (button){
                case HOME_SCREEN:{
                    SaveDataToStorage(false);
                    SetInterrupt(iCALIBRATE_EXIT);
                    break;
                }
                case DEMODULATION:{
                    // Rotate through the modulation types USB(0), LSB(1), AM(2), and SAM(3)
                    int8_t newmod = (int8_t)ED.modulation[ED.activeVFO] + 1;
                    if (newmod > (int8_t)SAM)
                        newmod = (int8_t)USB;
                    ED.modulation[ED.activeVFO] = (ModulationType)newmod;
                    UpdateFIRFilterMask(&RXfilters);
                    Debug("Modulation is " + String(ED.modulation[ED.activeVFO]));
                    break;
                }
                case 15:{
                    ChangeFrequencyCorrectionFactorIncrement();
                    break;
                }
                default:
                    break;
            }
            break;
        } // end of CALIBRATE_FREQUENCY
        case (UISm_StateId_CALIBRATE_RX_IQ):{
            switch (button){
                case HOME_SCREEN:{
                    // Force a save here
                    SaveDataToStorage(false);
                    SetInterrupt(iCALIBRATE_EXIT);
                    break;
                }
                case 15:{
                    ChangeRXIQIncrement();
                    break;
                }
                case 16:{
                    // Change to the first band
                    ED.currentBand[ED.activeVFO] = FIRST_BAND;
                    ED.centerFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][0];
                    ED.fineTuneFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][1];
                    ED.modulation[ED.activeVFO] = (ModulationType)ED.lastFrequencies[ED.currentBand[ED.activeVFO]][2];
                    UpdateRFHardwareState();
                    // Engage autotune
                    EngageRXIQAutotune();
                    break;
                }
                case BAND_UP:{
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
                    if(--ED.currentBand[ED.activeVFO] < FIRST_BAND)
                        ED.currentBand[ED.activeVFO] = LAST_BAND;
                    ED.centerFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][0];
                    ED.fineTuneFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][1];
                    ED.modulation[ED.activeVFO] = (ModulationType)ED.lastFrequencies[ED.currentBand[ED.activeVFO]][2];
                    UpdateRFHardwareState();
                    Debug("Band is " + String(bands[ED.currentBand[ED.activeVFO]].name));
                    break;
                }
                default:
                    break;
            }
            break;
        } // end of CALIBRATE_RX_IQ
        case (UISm_StateId_CALIBRATE_TX_IQ):{
            switch (button){
                case HOME_SCREEN:{
                    // Force a save here
                    SaveDataToStorage(false);
                    SetInterrupt(iCALIBRATE_EXIT);
                    break;
                }
                case 15:{
                    ChangeTXIQIncrement();
                    break;
                }
                case BAND_UP:{
                    if(++ED.currentBand[ED.activeVFO] > LAST_BAND)
                        ED.currentBand[ED.activeVFO] = FIRST_BAND;
                    ED.centerFreq_Hz[ED.activeVFO] = (bands[ED.currentBand[ED.activeVFO]].fBandHigh_Hz+bands[ED.currentBand[ED.activeVFO]].fBandLow_Hz)/2 + SR[SampleRate].rate/4;
                    ED.fineTuneFreq_Hz[ED.activeVFO] = 0;
                    ED.modulation[ED.activeVFO] = bands[ED.currentBand[ED.activeVFO]].mode;
                    UpdateRFHardwareState();
                    Debug("Band is " + String(bands[ED.currentBand[ED.activeVFO]].name));
                    break;
                }
                case BAND_DN:{
                    if(--ED.currentBand[ED.activeVFO] < FIRST_BAND)
                        ED.currentBand[ED.activeVFO] = LAST_BAND;
                    ED.centerFreq_Hz[ED.activeVFO] = (bands[ED.currentBand[ED.activeVFO]].fBandHigh_Hz+bands[ED.currentBand[ED.activeVFO]].fBandLow_Hz)/2 + SR[SampleRate].rate/4;
                    ED.fineTuneFreq_Hz[ED.activeVFO] = 0;
                    ED.modulation[ED.activeVFO] = bands[ED.currentBand[ED.activeVFO]].mode;
                    UpdateRFHardwareState();
                    Debug("Band is " + String(bands[ED.currentBand[ED.activeVFO]].name));
                    break;
                }
                default:
                    break;
            }
            break;
        } // end of CALIBRATE_TX_IQ
        case (UISm_StateId_CALIBRATE_POWER):{
            switch (button){
                case HOME_SCREEN:{
                    SetInterrupt(iCALIBRATE_EXIT);
                    break;
                }
                default:
                    break;
            }
            break;
        } // end of CALIBRATE_POWER
        default:{
            break; // ignore button presses
        }
    } // end of UI state machine cases
}

/**
 * Handle iambic keyer paddle events with special timing considerations.
 *
 * The iambic keyer state machine requires special handling because paddle events
 * may arrive while the state machine is still processing previous dit/dah sequences.
 * This function implements the "memory" feature of iambic keyers:
 *
 * State-dependent behavior:
 * - CW_RECEIVE or CW_TRANSMIT_KEYER_WAIT: Process paddle press immediately
 * - CW_TRANSMIT_DIT_MARK, CW_TRANSMIT_DAH_MARK, or CW_TRANSMIT_KEYER_SPACE:
 *   Prepend interrupt to FIFO head so it's processed as soon as current element completes
 * - All other states: Discard the interrupt (not in keyer mode)
 *
 * Supports ED.keyerFlip to swap dit/dah paddle assignments for left/right-handed operators.
 *
 * @param interrupt The keyer interrupt (iKEY1_PRESSED or iKEY2_PRESSED)
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

int32_t oldband = ED.currentBand[ED.activeVFO];
/**
 * Change the band if we tune out of the current band. However,
 * if we tune to a frequency outside the ham bands, keep the last
 * valid band setting to keep demodulation working.
 * */
void AdjustBand(void){
    oldband = ED.currentBand[ED.activeVFO];
    int32_t newband = GetBand(GetTXRXFreq(ED.activeVFO));
    if (newband != -1){
        ED.currentBand[ED.activeVFO] = newband;
        oldband = newband;
    }
}

// Forward-declare functions defined in MainBoard_DisplayCalibration.cpp
void IncrementRXIQPhase(void);
void DecrementRXIQPhase(void);
void IncrementRXIQAmp(void);
void DecrementRXIQAmp(void);
void IncrementTXIQPhase(void);
void DecrementTXIQPhase(void);
void IncrementTXIQAmp(void);
void DecrementTXIQAmp(void);
void IncrementTransmitAtt(void);
void DecrementTransmitAtt(void);
void IncrementEqualizerValue(void);
void DecrementEqualizerValue(void);
void IncrementEqualizerSelection(void);
void DecrementEqualizerSelection(void);

/**
 * Considers the next interrupt from the FIFO buffer and acts accordingly by either 
 * issuing an event to the state machines or by updating a system parameter. Interrupt 
 * is consumed and removed from the buffer.
 */
void ConsumeInterrupt(void){
    InterruptType interrupt = GetInterrupt();
    if ( interrupt == iNONE ) return;

    // Handle the interrupts created by the encoders slightly differently. The outcome
    // of these events depends on the UI state. All other interrupts are ignored by this
    // switch block and are handled by the switch block below.

    // Define what happens with encoder interrupt events in this switch block
    switch (uiSM.state_id){
        case (UISm_StateId_HOME):{
            switch (interrupt){
                case (iFILTER_INCREASE):{
                    FilterSetSSB(5,changeFilterHiCut);
                    break;
                }
                case (iFILTER_DECREASE):{
                    FilterSetSSB(-5,changeFilterHiCut);
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
                            if (ED.sidetoneVolume > 500) 
                                ED.sidetoneVolume = 500; // 0 to 500 range
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                } // end of VOLUME_INCREASE, HOME state
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
                            if (ED.currentMicGain < 0) 
                                ED.currentMicGain = 0;
                            break;
                        }
                        case SidetoneVolume:{
                            ED.sidetoneVolume -= 1.0;
                            if (ED.sidetoneVolume < 0) 
                                ED.sidetoneVolume = 0; // 0 to 500 range
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                } // end of VOLUME_DECREASE, HOME state
                case (iCENTERTUNE_INCREASE):{
                    ED.centerFreq_Hz[ED.activeVFO] += (int64_t)ED.freqIncrement;
                    // Change the band if we tune out of the current band. However,
                    // if we tune to a frequency outside the ham bands, keep the last
                    // valid band setting to keep demodulation working.
                    UpdateRFHardwareState();
                    break;
                }
                case (iCENTERTUNE_DECREASE):{
                    ED.centerFreq_Hz[ED.activeVFO] -= (int64_t)ED.freqIncrement;
                    // check for minimum frequency supported by Si5351 quadrature signal generator
                    if (ED.centerFreq_Hz[ED.activeVFO] < 250000)
                        ED.centerFreq_Hz[ED.activeVFO] = 250000;
                    UpdateRFHardwareState();
                    break;
                }
                case (iFINETUNE_INCREASE):{
                    AdjustFineTune(+1);
                    break;
                }
                case (iFINETUNE_DECREASE):{
                    AdjustFineTune(-1);
                    break;
                }
                default: // do nothing and handle these below
                    break;
            }
            break;
        }
        case (UISm_StateId_UPDATE):{
            switch (interrupt){
                case (iFILTER_INCREASE):{
                    IncrementValue();
                    break;
                }
                case (iFILTER_DECREASE):{
                    DecrementValue();
                    break;
                }
                default: // handle them later
                    break;
            }
            break;
        } // end of UPDATE state, encoder interrupts
        case (UISm_StateId_MAIN_MENU):{
            switch (interrupt){
                case (iFILTER_INCREASE):{
                    IncrementPrimaryMenu();
                    break;
                }
                case (iFILTER_DECREASE):{
                    DecrementPrimaryMenu();
                    break;
                }
                default: // handle them later
                    break;
            }
            break;
        } // end of MAIN_MENU state, encoder interrupts
        case (UISm_StateId_SECONDARY_MENU):{
            switch (interrupt){
                case (iFILTER_INCREASE):{
                    IncrementSecondaryMenu();
                    break;
                }
                case (iFILTER_DECREASE):{
                    DecrementSecondaryMenu();
                    break;
                }
                default: // handle them later
                    break;
            }
            break;
        } // end of SECONDARY_MENU state, encoder interrupts
        case (UISm_StateId_EQUALIZER):{
            switch (interrupt){
                case (iFILTER_INCREASE):{
                    IncrementEqualizerValue();
                    break;
                }
                case (iFILTER_DECREASE):{
                    DecrementEqualizerValue();
                    break;
                }
                case (iVOLUME_INCREASE):{
                    IncrementEqualizerSelection();
                    break;
                }
                case (iVOLUME_DECREASE):{
                    DecrementEqualizerSelection();
                    break;
                }
                default: // handle them later
                    break;
            }
            break;
        } // end of EQUALIZER state, encoder interrupts
        case (UISm_StateId_CALIBRATE_RX_IQ):{
            switch (interrupt){
                case (iFILTER_INCREASE):{
                    IncrementRXIQPhase();
                    break;
                }
                case (iFILTER_DECREASE):{
                    DecrementRXIQPhase();
                    break;
                }
                case (iVOLUME_INCREASE):{
                    IncrementRXIQAmp();
                    break;
                }
                case (iVOLUME_DECREASE):{
                    DecrementRXIQAmp();
                    break;
                }
                default: // handle them later
                    break;
            }
            break;
        } // end of calibrate RX IQ encoder interrupts

        case (UISm_StateId_CALIBRATE_TX_IQ):{
            switch (interrupt){
                case (iFILTER_INCREASE):{
                    IncrementTXIQPhase();
                    break;
                }
                case (iFILTER_DECREASE):{
                    DecrementTXIQPhase();
                    break;
                }
                case (iVOLUME_INCREASE):{
                    IncrementTXIQAmp();
                    break;
                }
                case (iVOLUME_DECREASE):{
                    DecrementTXIQAmp();
                    break;
                }
                case (iFINETUNE_INCREASE):{
                    IncrementTransmitAtt();
                    break;
                }
                case (iFINETUNE_DECREASE):{
                    DecrementTransmitAtt();
                    break;
                }

                default: // handle them later
                    break;
            }
            break;
        } // end of calibrate TX IQ encoder interrupts

        case (UISm_StateId_CALIBRATE_FREQUENCY):{
            switch (interrupt){
                case (iFILTER_INCREASE):{
                    IncreaseFrequencyCorrectionFactor();
                    break;
                }
                case (iFILTER_DECREASE):{
                    DecreaseFrequencyCorrectionFactor();
                    break;
                }
                default: // handle them later
                    break;
            }
            break;
        } // end of calibrate frequency encoder interrupts

        default:
            break;
    }
    // end of encoder interrupt events switch block

    // Handle all the other non-encoder interrupts
    switch (interrupt){
        case (iBUTTON_PRESSED):{
            int32_t button = GetButton();
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
            if (ED.keyType == KeyTypeId_Keyer)
                HandleKeyer(interrupt);
            break;
        }
        case (iEQUALIZER):{
            UISm_dispatch_event(&uiSM,UISm_EventId_EQUALIZER);
            break;
        }
        case (iCALIBRATE_FREQUENCY):{
            UISm_dispatch_event(&uiSM,UISm_EventId_CALIBRATE_FREQUENCY);
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_FREQUENCY);
            break;
        }
        case (iCALIBRATE_RX_IQ):{
            UISm_dispatch_event(&uiSM,UISm_EventId_CALIBRATE_RX_IQ);
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_RX_IQ);  
            break;
        }
        case (iCALIBRATE_TX_IQ):{
            UISm_dispatch_event(&uiSM,UISm_EventId_CALIBRATE_TX_IQ);
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_TX_IQ);  
            break;
        }
        case (iCALIBRATE_CW_PA):{
            UISm_dispatch_event(&uiSM,UISm_EventId_CALIBRATE_POWER);
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_CW_PA);     
            break;
        }
        case (iCALIBRATE_EXIT):{
            // Go back to the home screen
            UISm_dispatch_event(&uiSM,UISm_EventId_HOME);
            // Exit the calibration mode
            ModeSm_dispatch_event(&modeSM,ModeSm_EventId_CALIBRATE_EXIT);
            break;
        }
        default:
            break;
    }
    // end of non-encoder interrupt events switch block
}

///////////////////////////////////////////////////////////////////////////////
// The main loop
///////////////////////////////////////////////////////////////////////////////

/**
 * Perform graceful shutdown sequence when power-off is requested.
 *
 * Called when the external power management circuit (ATTiny) signals that the
 * power button has been pressed. This function:
 * 1. Saves all radio state to persistent storage (EEPROM)
 * 2. Signals shutdown completion to the ATTiny via SHUTDOWN_COMPLETE pin
 * 3. Waits for the ATTiny to cut power to the Teensy
 *
 * This function does not return - power will be cut during the delay.
 *
 * @note This is a blocking function that delays for 1 second
 */
void ShutdownTeensy(void){
    // Do whatever is needed before cutting power here
    SaveDataToStorage(true);
    
    // Tell the ATTiny that we have finished shutdown and it's safe to power off
    digitalWrite(SHUTDOWN_COMPLETE, 1);
    MyDelay(1000); // wait for the turn off command
}

/**
 * Main program loop - executed repeatedly while radio is powered on.
 *
 * This is the central execution loop of the Phoenix SDR firmware. It runs continuously
 * and must complete each iteration within ~10ms to prevent audio buffer overflow.
 *
 * The loop performs these steps in order:
 * 1. Monitor for shutdown signal from power management circuit
 * 2. Process CW key debouncing (main loop polling for stable state)
 * 3. Check front panel for button/encoder events
 * 4. Check CAT serial interface for computer control commands
 * 5. Consume and process next interrupt event from FIFO
 * 6. Perform real-time DSP on audio buffers
 * 7. Update display with current radio state
 *
 * Execution Constraints:
 * - FASTRUN annotation places this function in RAM for maximum speed
 * - Target execution time: < 10ms per iteration to maintain audio streaming
 * - All operations must be non-blocking or have bounded execution time
 *
 * @note This function never returns under normal operation
 * @see PerformSignalProcessing() for DSP implementation
 * @see ConsumeInterrupt() for event processing
 */
FASTRUN void loop(void){
    // Check for signal to begin shutdown and perform shutdown routine if requested
    if (digitalRead(BEGIN_TEENSY_SHUTDOWN)) ShutdownTeensy();

    // Step 1: Check for new events and handle them
    ProcessKey1Debounce();
    ProcessPTTDebounce();
    CheckForFrontPanelInterrupts();
    CheckForCATSerialEvents();
    ConsumeInterrupt();
    
    // Step 2: Perform signal processing
    PerformSignalProcessing();

    // Step 3: Draw the display
    DrawDisplay();
}