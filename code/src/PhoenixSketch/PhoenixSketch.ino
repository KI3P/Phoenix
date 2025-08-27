#include "SDT.h"

void setup(void){
    Serial.begin(115200);
    delay(1000); //testcode
    Serial.println("T41 SDT Setup");
    //Configure the pins for the auto shutdown
    pinMode(BEGIN_TEENSY_SHUTDOWN, INPUT);  // HI received here tells Teensy to start shutdown routine
    pinMode(SHUTDOWN_COMPLETE, OUTPUT);     // HI sent here tells ATTiny85 to cut the power
    digitalWrite(SHUTDOWN_COMPLETE, 0);  
    
    pinMode(31,OUTPUT); //testcode
    digitalWrite(31, 0);  //testcode

    FrontPanelInit();

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.agc = AGCOff;
    ED.nrOptionSelect = NROff;
    SetupAudio();
    UpdateAudioIOState();

    InitializeRFBoard();
    InitializeSignalProcessing();
    // If our input tone was at 1 kHz, then it will appear at 49 kHz after Fs/4
    // So make our VFO frequency the negative of this to shift it back to 1 kHz
    ED.fineTuneFreq_Hz = -48000.0; //testcode

}