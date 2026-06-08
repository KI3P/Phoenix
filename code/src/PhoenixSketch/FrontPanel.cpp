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

#include "FrontPanel.h"

#define LED1 0
#define LED2 1
#define LED_1_PORT 6
#define LED_2_PORT 7
#define INT_PIN_1 14
#define INT_PIN_2 15

enum {
  PRESSED,
  RELEASED
};
#define DEBOUNCE_DELAY 250

static Adafruit_MCP23X17 mcp1;
static volatile bool interrupted1 = false;

static Adafruit_MCP23X17 mcp2;
static volatile bool interrupted2 = false;

// Drains the front-panel MCP23017s on a fixed 1 ms tick, decoupled from the main
// loop's display/DSP cadence. Runs at NVIC priority 240 (see InitializeFrontPanel),
// i.e. BELOW the OpenAudio update ISR (priority 208), so encoder/button I2C work
// can never delay audio servicing.
static IntervalTimer fpServiceTimer;

static int32_t button_press_ms;
static int32_t ButtonPressed = -1;

#define e1 volumeEncoder
#define e2 filterEncoder
#define e3 tuneEncoder
#define e4 fineTuneEncoder

static Rotary_V12 volumeEncoder( VOLUME_REVERSED );
static Rotary_V12 filterEncoder( FILTER_REVERSED );
static Rotary_V12 tuneEncoder( MAIN_TUNE_REVERSED );
static Rotary_V12 fineTuneEncoder( FINE_TUNE_REVERSED );

/**
 * Get the currently pressed button number.
 *
 * @return Button number (0-15 for MCP1, 16-21 for MCP2), or -1 if no button pressed
 */
int32_t GetButton(void){
    return ButtonPressed;
}

/**
 * Set the button pressed state.
 *
 * @param bt Button number to set, or -1 to clear the button pressed state
 */
void SetButton(int32_t bt){
    ButtonPressed = bt;
}

/**
 * Set the state of a front panel LED.
 *
 * @param led LED number (LED1=0 or LED2=1)
 * @param state LED state (HIGH/LOW)
 */
FASTRUN
void FrontPanelSetLed(uint8_t led, uint8_t state) {
    switch (led) {
        case LED1:
            mcp2.digitalWrite(LED_1_PORT, state);
            break;
        case LED2:
            mcp2.digitalWrite(LED_2_PORT, state);
            break;
    }
}

/**
 * Interrupt handler for MCP23017 #1 (switches 1-16).
 * Reads button presses from pins 0-15 on the first MCP23017,
 * applies debouncing, and sets the iBUTTON_PRESSED interrupt.
 */
FASTRUN
static void interrupt1() {
    uint8_t pin;
    uint8_t state;
    while((pin = mcp1.getLastInterruptPin())!=MCP23XXX_INT_ERR) {
        state = mcp1.digitalRead(pin);
        if (state == PRESSED) {
            if ((millis()-button_press_ms)>DEBOUNCE_DELAY){
                ButtonPressed = pin;
                button_press_ms = millis();
                SetInterrupt(iBUTTON_PRESSED);
            }
        }
    }
    mcp1.clearInterrupts();
}

/**
 * Interrupt handler for MCP23017 #2 (switches 17-22, encoders, LEDs).
 * Handles:
 * - Switches 17-18 and encoder switches on pins 0-5 (port A)
 * - Four rotary encoders on pins 8-15 (port B)
 * - Generates appropriate encoder and button press interrupts
 */
FASTRUN
static void interrupt2() {
    uint8_t pin;
    uint8_t state = 0x00;
    uint8_t a_state;
    uint8_t b_state;
    uint16_t both;

    while((pin = mcp2.getLastInterruptPin())!=MCP23XXX_INT_ERR) {
        both = mcp2.readGPIOAB(); // save an I2C instruction for speed
        // A is lower 8, B is upper 8
        a_state = (both >> 0) & 0xFF;
        b_state = (both >> 8) & 0xFF;
        switch(pin) {
            case 8:
            case 9:
                state = b_state & 0x03;
                break;
            case 10:
            case 11:
                state = (b_state >> 2) & 0x03;
                break;
            case 12:
            case 13:
                state = (b_state >> 4) & 0x03;
                break;
            case 14:
            case 15:
                state = (b_state >> 6) & 0x03;
                break;
        }

        // process the state
        int change;
        switch(pin) {
            case 8:
                e1.updateA(state);
                change = e1.process();
                if (change>0) SetInterrupt(iVOLUME_INCREASE);
                if (change<0) SetInterrupt(iVOLUME_DECREASE);
                break;
            case 9:
                e1.updateB(state);
                change = e1.process();
                if (change>0) SetInterrupt(iVOLUME_INCREASE);
                if (change<0) SetInterrupt(iVOLUME_DECREASE);
                break;
            case 10:
                e2.updateA(state);
                change = e2.process();
                if (change>0) SetInterrupt(iFILTER_INCREASE);
                if (change<0) SetInterrupt(iFILTER_DECREASE);
                break;
            case 11:
                e2.updateB(state);
                change = e2.process();
                if (change>0) SetInterrupt(iFILTER_INCREASE);
                if (change<0) SetInterrupt(iFILTER_DECREASE);
                break;
            case 12:
                e3.updateA(state);
                change = e3.process();
                if (change>0) SetInterrupt(iCENTERTUNE_INCREASE);
                if (change<0) SetInterrupt(iCENTERTUNE_DECREASE);
                break;
            case 13:
                e3.updateB(state);
                change = e3.process();
                if (change>0) SetInterrupt(iCENTERTUNE_INCREASE);
                if (change<0) SetInterrupt(iCENTERTUNE_DECREASE);
                break;
            case 14:
                e4.updateA(state);
                change = e4.process();
                if (change>0) SetInterrupt(iFINETUNE_INCREASE);
                if (change<0) SetInterrupt(iFINETUNE_DECREASE);
                break;
            case 15:
                e4.updateB(state);
                change = e4.process();
                if (change>0) SetInterrupt(iFINETUNE_INCREASE);
                if (change<0) SetInterrupt(iFINETUNE_DECREASE);
                break;
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
                // Pins 1 through 6 are SW17, SW18, then switches on 4 encoders
                state = (a_state >> pin) & 0x01;
                if (state == PRESSED) {
                    if ((millis()-button_press_ms)>DEBOUNCE_DELAY){
                        ButtonPressed = (pin+16);
                        button_press_ms = millis();
                        SetInterrupt(iBUTTON_PRESSED);
                    }
                }
                break;
            default:
                // 255 sometimes caused by switch bounce
                Debug(String(__FUNCTION__)+": "+String(pin)+"!");
                break;
        }
    }
    mcp2.clearInterrupts();
}

/**
 * Hardware ISR for the MCP23017 #1 INT line (pin 14, open-drain active-LOW).
 * Deliberately minimal: it only records that an interrupt is pending. The actual
 * I2C servicing happens later in ServiceFrontPanel() at a lower priority, so we
 * never perform a blocking I2C transfer inside a high-priority GPIO ISR.
 */
FASTRUN
static void fpInterrupt1ISR(void) {
    interrupted1 = true;
}

/**
 * Hardware ISR for the MCP23017 #2 INT line (pin 15, open-drain active-LOW).
 * See fpInterrupt1ISR() - flag only, no I2C here.
 */
FASTRUN
static void fpInterrupt2ISR(void) {
    interrupted2 = true;
}

/**
 * Timer-driven front-panel service routine (1 ms tick, NVIC priority 240).
 * Performs the deferred I2C drain for whichever MCP23017 has signalled. The pin
 * level is checked as well as the flag: the MCP INT line stays asserted (LOW)
 * until its GPIO is read, so testing the level recovers any edge the ISR could
 * have missed while the line was already low.
 */
FASTRUN
static void ServiceFrontPanel(void) {
    if (interrupted1 || digitalRead(INT_PIN_1) == 0) {
        interrupted1 = false;
        interrupt1();
    }
    if (interrupted2 || digitalRead(INT_PIN_2) == 0) {
        interrupted2 = false;
        interrupt2();
    }
}

/**
 * Initialize the front panel hardware.
 * Configures two MCP23017 I2C GPIO expanders for:
 * - MCP1: 16 front panel switches (pins 0-15)
 * - MCP2: 6 switches (pins 0-5), 2 LEDs (pins 6-7), 4 rotary encoders (pins 8-15)
 * Sets up interrupt-on-change for all inputs and configures debouncing.
 */
void InitializeFrontPanel(void) {
    bool failed=false;
    Debug("Initializing front panel");

    bit_results.FRONT_PANEL_I2C_present = true;
    Wire1.begin();

    if (!mcp1.begin_I2C(V12_PANEL_MCP23017_ADDR_1,&Wire1)) {
        failed=true;
        bit_results.FRONT_PANEL_I2C_present = false;
    }

    if (!mcp2.begin_I2C(V12_PANEL_MCP23017_ADDR_2,&Wire1)) {
        failed=true;
        bit_results.FRONT_PANEL_I2C_present = false;
    }

    if(failed) return;

    // The front-panel MCP23017s are the only devices on Wire1, so raise the bus
    // clock from the 100 kHz default to 400 kHz (Fast-mode). This shortens every
    // encoder/button register read, letting fast encoder edges be drained quicker.
    Wire1.setClock(400000);

    // setup the mcp23017 devices
    mcp1.setupInterrupts(true, true, LOW);
    mcp2.setupInterrupts(true, true, LOW);
    // setup switches 1..16
    for (int i = 0; i < 16; i++) {
        mcp1.pinMode(i, INPUT_PULLUP);
        mcp1.setupInterruptPin(i, CHANGE);
    }

    // setup switches 17..18 and Encoder switches 1..4 (note 6 and 7 are output LEDs)
    for (int i = 0; i < 6; i++) {
        mcp2.pinMode(i, INPUT_PULLUP);
        mcp2.setupInterruptPin(i, CHANGE);
    }
    mcp2.pinMode(LED_1_PORT, OUTPUT);  // LED1
    mcp2.digitalWrite(LED_1_PORT, LOW);
    mcp2.pinMode(LED_2_PORT, OUTPUT);  // LED2
    mcp2.digitalWrite(LED_2_PORT, LOW);   

    // setup encoders 1..4 A and B
    for (int i = 8; i < 16; i++) {
        mcp2.pinMode(i, INPUT_PULLUP);
        mcp2.setupInterruptPin(i, CHANGE);
    }
    // clear interrupts
    mcp1.readGPIOAB(); // ignore any return value
    mcp2.readGPIOAB(); // ignore any return value

    // Configure pins to check for button press interrupts
    pinMode(INT_PIN_1, INPUT_PULLUP);
    pinMode(INT_PIN_2, INPUT_PULLUP);

    // Flag-setting ISRs on the MCP INT lines so an encoder edge is noticed
    // immediately, instead of waiting for the next main-loop poll.
    attachInterrupt(digitalPinToInterrupt(INT_PIN_1), fpInterrupt1ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(INT_PIN_2), fpInterrupt2ISR, FALLING);

    // Drain the front panel every 1 ms at a priority below the audio update ISR.
    // NOTE: all IntervalTimers share IRQ_PIT, whose priority is forced to the
    // highest (lowest-numbered) requester, so timer1ms is also lowered to 240 in
    // setup() (Globals.cpp) - otherwise this drain would run at 128, above audio.
    fpServiceTimer.begin(ServiceFrontPanel, 1000); // microseconds -> 1 ms
    fpServiceTimer.priority(240);
}

/**
 * Poll for and process front panel interrupts.
 * Checks the interrupt pins for both MCP23017 devices and calls
 * the appropriate interrupt handlers to process button presses
 * and encoder changes. Should be called regularly from main loop.
 */
void CheckForFrontPanelInterrupts(void){
    if (digitalRead(INT_PIN_1) == 0){
        // We received an interrupt on pin 1
        interrupt1();
    }
    if (digitalRead(INT_PIN_2) == 0){
        // We received an interrupt on pin 2
        interrupt2();
    }
}
