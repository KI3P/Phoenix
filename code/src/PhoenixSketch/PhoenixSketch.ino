#include "SDT.h"

// Create an IntervalTimer object for driving the state machines
IntervalTimer timer1ms;

/**
 * This is run every 1ms. It dispatches do events to the state machines
 */
void tick1ms(void){
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    UISm_dispatch_event(&uiSM, UISm_EventId_DO);
}

const unsigned long DEBOUNCE_DELAY = 50; // 50ms debounce time
volatile bool lastKey1State = LOW;
volatile uint32_t lastKey1time;
void Key1Change(void){
    uint32_t currentTime = millis();
    // Check if enough time has passed since last interrupt
    if (currentTime - lastKey1time < DEBOUNCE_DELAY) {
        return; // Ignore this interrupt (likely bounce)
    }

    bool currentState = digitalRead(KEY1);
    if (currentState != lastKey1State) {
        if (currentState == HIGH) {
            // Rising edge detected
            SetInterrupt(iKEY1_RELEASED);
        } else {
            // Falling edge detected  
            SetInterrupt(iKEY1_PRESSED);
        }
        lastKey1State = currentState;
    }
    lastKey1time = currentTime;
}

volatile uint32_t lastKey2time;
void Key2On(void){
    uint32_t currentTime = millis();
    // Check if enough time has passed since last interrupt
    if (currentTime - lastKey2time < DEBOUNCE_DELAY) {
        return; // Ignore this interrupt (likely bounce)
    }
    SetInterrupt(iKEY2_PRESSED);
    lastKey2time = currentTime;
}

void Flag(uint8_t val){
    digitalWrite(31, (val >> 0) & 0b1);  //testcode
    digitalWrite(30, (val >> 1) & 0b1);  //testcode
    digitalWrite(29, (val >> 2) & 0b1);  //testcode
    digitalWrite(28, (val >> 3) & 0b1);  //testcode
}

void setup(void){
    Serial.begin(115200);
    SerialUSB1.begin(38400); // For CAT control
    //delay(1000); //testcode
    Serial.println("T41 SDT Setup");
    //Configure the pins for the auto shutdown
    pinMode(BEGIN_TEENSY_SHUTDOWN, INPUT);  // HI received here tells Teensy to start shutdown routine
    pinMode(SHUTDOWN_COMPLETE, OUTPUT);     // HI sent here tells ATTiny85 to cut the power
    digitalWrite(SHUTDOWN_COMPLETE, 0);  
    
    pinMode(31,OUTPUT); //testcode
    pinMode(30,OUTPUT); //testcode
    pinMode(29,OUTPUT); //testcode
    pinMode(28,OUTPUT); //testcode
    Flag(0);
    Serial.println("...Initializing storage");
    InitializeStorage();

    Serial.println("...Initializing hardware");
    InitializeFrontPanel();
    InitializeSignalProcessing();  // Initialize DSP before starting audio
    InitializeAudio();
    InitializeDisplay();
    InitializeRFHardware(); // RF board, LPF board, and BPF board
    
    // Initialize temperature monitoring
    uint16_t temp_check_frequency = 0x03U;  //updates the temp value at a RTC/3 clock rate
    //0xFFFF determines a 2 second sample rate period
    uint32_t highAlarmTemp = 85U;  //42 degrees C
    uint32_t lowAlarmTemp = 25U;
    uint32_t panicAlarmTemp = 90U;
    initTempMon(temp_check_frequency, lowAlarmTemp, highAlarmTemp, panicAlarmTemp);
    // this starts the measurements
    TEMPMON_TEMPSENSE0 |= 0x2U;

    // Start the mode state machines
    Serial.println("...Starting state machines");
    modeSM.vars.waitDuration_ms = CW_TRANSMIT_SPACE_TIMEOUT_MS;
    modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
    ModeSm_start(&modeSM);
    ED.agc = AGCOff;
    ED.nrOptionSelect = NROff;
    uiSM.vars.splashDuration_ms = SPLASH_DURATION_MS;
    UISm_start(&uiSM);

    UpdateAudioIOState();

    // If our input tone was at 1 kHz, then it will appear at 49 kHz after Fs/4
    // So make our VFO frequency the negative of this to shift it back to 1 kHz
    ED.fineTuneFreq_Hz[ED.activeVFO] = -48000L; //testcode

    Serial.println("...Setting up key interrupts");
    // Set up interrupts for key
    pinMode(KEY1, INPUT_PULLUP);
    pinMode(KEY2, INPUT_PULLUP);
    lastKey1State = digitalRead(KEY1); // Initialize
    lastKey1time = millis(); // for debounce
    attachInterrupt(digitalPinToInterrupt(KEY1), Key1Change, CHANGE);
    lastKey2time = millis(); // for debounce
    attachInterrupt(digitalPinToInterrupt(KEY2), Key2On, FALLING);

    timer1ms.begin(tick1ms, 1000);  // run tick1ms every 1ms
    
    Serial.println("...Setup done!");
}
