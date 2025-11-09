#ifndef LOOP_H
#define LOOP_H

/**
 * @brief Hardware interrupt event types for event-driven architecture
 * @note Events are queued in FIFO buffer by interrupt handlers and consumed by main loop
 * @note Includes PTT, CW key, encoder, button, and calibration events
 */
typedef enum {
    iNONE,                    /**< No interrupt event (buffer empty) */
    iPTT_PRESSED,             /**< Push-to-talk button pressed */
    iPTT_RELEASED,            /**< Push-to-talk button released */
    iMODE,                    /**< Radio operating mode changed */
    iCALIBRATE_CW_PA,         /**< Calibrate CW power amplifier */
    iCALIBRATE_EXIT,          /**< Exit calibration mode */
    iCALIBRATE_FREQUENCY,     /**< Calibrate frequency reference */
    iCALIBRATE_RX_IQ,         /**< Calibrate receiver I/Q balance */
    iCALIBRATE_SSB_PA,        /**< Calibrate SSB power amplifier */
    iCALIBRATE_TX_IQ,         /**< Calibrate transmitter I/Q balance */
    iKEY1_PRESSED,            /**< CW key 1 pressed (primary paddle or straight key) */
    iKEY1_RELEASED,           /**< CW key 1 released */
    iKEY2_PRESSED,            /**< CW key 2 pressed (secondary paddle for iambic keyer) */
    iVOLUME_INCREASE,         /**< Volume encoder rotated clockwise */
    iVOLUME_DECREASE,         /**< Volume encoder rotated counter-clockwise */
    iFILTER_INCREASE,         /**< Filter encoder rotated clockwise */
    iFILTER_DECREASE,         /**< Filter encoder rotated counter-clockwise */
    iCENTERTUNE_INCREASE,     /**< Main tuning encoder rotated clockwise */
    iCENTERTUNE_DECREASE,     /**< Main tuning encoder rotated counter-clockwise */
    iFINETUNE_INCREASE,       /**< Fine tuning encoder rotated clockwise */
    iFINETUNE_DECREASE,       /**< Fine tuning encoder rotated counter-clockwise */
    iBUTTON_PRESSED,          /**< Front panel button pressed */
    iVFO_CHANGE,              /**< Active VFO changed (A/B toggle) */
    iUPDATE_TUNE,             /**< Request VFO frequency update */
    iMODE_CHANGE,             /**< Operating mode changed (SSB/CW) */
    iPOWER_CHANGE             /**< Transmit power level changed */
} InterruptType;

/**
 * @brief Process next interrupt from FIFO buffer and dispatch to state machines
 * @note Consumes interrupt event and removes it from buffer
 * @note Routes events to ModeSm, UISm, or directly updates system parameters
 * @note Handles PTT, CW keys, encoders, buttons, and calibration events
 */
void ConsumeInterrupt(void);

/**
 * @brief Retrieve next interrupt from FIFO buffer without consuming
 * @return Next InterruptType from buffer head, or iNONE if buffer is empty
 * @note Does not remove interrupt from buffer - use ConsumeInterrupt() for normal processing
 */
InterruptType GetInterrupt(void);

/**
 * @brief Add interrupt event to end of FIFO buffer
 * @param i Interrupt event type to enqueue
 * @note Called by interrupt handlers to queue hardware events for main loop processing
 * @note If buffer is full (16 events), oldest event is dropped
 */
void SetInterrupt(InterruptType i);

/**
 * @brief Add interrupt event to beginning of FIFO buffer (priority queue)
 * @param i Interrupt event type to prepend
 * @note Used by iambic keyer to implement paddle "memory" feature
 * @note If buffer is full (16 events), oldest event is dropped
 */
void PrependInterrupt(InterruptType i);

/**
 * @brief Main program loop executed repeatedly while radio is powered on
 * @note FASTRUN annotation places function in RAM for maximum execution speed
 * @note Must complete each iteration within ~10ms to prevent audio buffer overflow
 * @note Execution sequence: shutdown check, CW key debounce, front panel poll, CAT poll, event processing, DSP, display update
 * @note This function never returns under normal operation
 */
void loop(void);

/**
 * @brief Configure CW key type for straight key or iambic keyer operation
 * @param key Key type: KeyTypeId_Straight for straight key, KeyTypeId_Keyer for iambic keyer
 * @note Changes how KEY1/KEY2 inputs are interpreted by the mode state machine
 */
void SetKeyType(KeyTypeId key);

/**
 * @brief Configure iambic keyer paddle assignment: KEY1=dit, KEY2=dah
 * @note Sets ED.keyerFlip to false for standard right-handed keyer operation
 * @note Only affects iambic keyer mode, not straight key operation
 */
void SetKey1Dit(void);

/**
 * @brief Configure iambic keyer paddle assignment: KEY1=dah, KEY2=dit
 * @note Sets ED.keyerFlip to true for left-handed keyer operation
 * @note Only affects iambic keyer mode, not straight key operation
 */
void SetKey1Dah(void);

/**
 * @brief Perform graceful shutdown sequence when power-off is requested
 * @note Saves all radio state to persistent storage (LittleFS and SD card)
 * @note Signals shutdown completion to ATTiny power management circuit
 * @note This function does not return - power is cut during 1-second delay
 */
void ShutdownTeensy(void);

/**
 * @brief Get current number of pending interrupt events in FIFO buffer
 * @return Number of queued interrupt events (0-16)
 * @note Used for diagnostics and buffer overflow detection
 */
size_t GetInterruptFifoSize(void);

/**
 * @brief Initialize GPIO pins and attach interrupt handlers for CW key inputs
 * @note Configures KEY1, KEY2, and PTT pins with internal pull-up resistors
 * @note Attaches interrupt handlers: KEY1 on CHANGE, KEY2 on FALLING, PTT on CHANGE
 * @note Must be called during initialization before entering main loop
 */
void SetupCWKeyInterrupts(void);

#endif // LOOP_H
