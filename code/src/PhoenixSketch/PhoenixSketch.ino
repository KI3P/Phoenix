#include "SDT.h"

// Create an IntervalTimer object for driving the state machines
IntervalTimer timer1ms;

/**
 * This is run every 1ms. It dispatches do events to the state machines which
 * are needed for time dependent state changes like the CW keyer
 */
void tick1ms(void){
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    UISm_dispatch_event(&uiSM, UISm_EventId_DO);
}

void setup(void){
    Serial.begin(115200);
    SerialUSB1.begin(38400); // For CAT control
    Serial.println("T41 SDT Setup");
    //Configure the pins for the auto shutdown
    pinMode(BEGIN_TEENSY_SHUTDOWN, INPUT);  // HI received here tells Teensy to start shutdown routine
    pinMode(SHUTDOWN_COMPLETE, OUTPUT);     // HI sent here tells ATTiny85 to cut the power
    digitalWrite(SHUTDOWN_COMPLETE, 0);  

    // Used to write the state to 4-bit readout for state timing measurements
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
    UpdateDitLength();
    ModeSm_start(&modeSM);
    uiSM.vars.splashDuration_ms = SPLASH_DURATION_MS;
    UISm_start(&uiSM);

    UpdateAudioIOState();

    Serial.println("...Setting up key interrupts");
    SetupCWKeyInterrupts();

    timer1ms.begin(tick1ms, 1000);  // run tick1ms every 1ms
    
    Serial.println("...Setup done!");
}
