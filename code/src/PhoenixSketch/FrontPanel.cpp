#include "FrontPanel.h"

enum {
  PRESSED,
  RELEASED
};
#define DEBOUNCE_DELAY 250

static Adafruit_MCP23X17 mcp1;
static volatile bool interrupted1 = false;

static Adafruit_MCP23X17 mcp2;
static volatile bool interrupted2 = false;

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

FASTRUN
static void interrupt1() {
    uint8_t pin;
    uint8_t state;
    __disable_irq();
    while((pin = mcp1.getLastInterruptPin())!=MCP23XXX_INT_ERR) {
        state = mcp1.digitalRead(pin);
        if (state == PRESSED) {
            if ((millis()-button_press_ms)>DEBOUNCE_DELAY){
                ButtonPressed = pin;
                button_press_ms = millis();
                Debug("Pressed button: " + String(ButtonPressed));
            }
        } 
    }
    __enable_irq();
}

FASTRUN
static void interrupt2() {
    uint8_t pin;
    uint8_t state = 0x00;
    uint8_t a_state;
    uint8_t b_state;

    __disable_irq();
    while((pin = mcp2.getLastInterruptPin())!=MCP23XXX_INT_ERR) {
        a_state = mcp2.readGPIOA();
        b_state = mcp2.readGPIOB();
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
                if (change!=0) Debug("e1 Volume change by " + String(change));
                break;
            case 9:
                e1.updateB(state);
                change = e1.process();
                if (change!=0) Debug("e1 Volume change by " + String(change));
                break;
            case 10:
                e2.updateA(state);
                change = e2.process();
                if (change!=0) Debug("e2 Filter change by " + String(change));
                break;
            case 11:
                e2.updateB(state);
                change = e2.process();
                if (change!=0) Debug("e2 Filter change by " + String(change));
                break;
            case 12:
                e3.updateA(state);
                change = e3.process();
                if (change!=0) Debug("e3 Tune change by " + String(change));
                break;
            case 13:
                e3.updateB(state);
                change = e3.process();
                if (change!=0) Debug("e3 Tune change by " + String(change));
                break;
            case 14:
                e4.updateA(state);
                change = e4.process();
                if (change!=0) Debug("e4 Fine change by " + String(change));
                break;
            case 15:
                e4.updateB(state);
                change = e4.process();
                if (change!=0) Debug("e4 Fine change by " + String(change));
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
                        Debug("Pressed button: " + String(ButtonPressed));
                    }
                }
                break;
            default:
                // 255 sometimes caused by switch bounce
                Debug(String(__FUNCTION__)+": "+String(pin)+"!");
                break;
        }
    }
    __enable_irq();
}

void FrontPanelInit(void) {
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

    pinMode(INT_PIN_1, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INT_PIN_1), interrupt1, LOW);

    pinMode(INT_PIN_2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INT_PIN_2), interrupt2, LOW);
  
}
