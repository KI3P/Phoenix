#include "gtest/gtest.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

#include "../src/PhoenixSketch/SDT.h"

// Mutex to protect buffer_add() from race conditions
static std::mutex buffer_mutex;

#define GETHWRBITS(LSB,len) ((hardwareRegister >> LSB) & ((1 << len) - 1))

// Timer variables for interrupt simulation
static std::atomic<bool> timer_running{false};
static std::thread timer_thread;

/**
 * Timer interrupt function that runs every 1ms
 * Dispatches DO events to the state machines
 */
void timer1ms(void) {
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    UISm_dispatch_event(&uiSM, UISm_EventId_DO);
}

/**
 * Start the 1ms timer interrupt
 */
void start_timer1ms() {
    if (timer_running.load()) return; // Already running

    timer_running.store(true);
    timer_thread = std::thread([]() {
        while (timer_running.load()) {
            timer1ms();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
}

/**
 * Stop the 1ms timer interrupt
 */
void stop_timer1ms() {
    if (!timer_running.load()) return; // Already stopped

    timer_running.store(false);
    if (timer_thread.joinable()) {
        timer_thread.join();
    }
}

// We can't override buffer_add due to linking conflicts.
// The issue is that the timer thread and main thread are calling buffer_add simultaneously.
// Let's analyze the actual timing output to understand the root cause better.

void CheckThatHardwareRegisterMatchesActualHardware(){
    // LPF
    uint16_t gpioab = GetLPFMCPRegisters(); // a is upper half, b is lower half
    EXPECT_EQ((uint8_t)(gpioab & 0x00FF), (uint8_t)(hardwareRegister & 0x000000FF)); // gpiob
    EXPECT_EQ((uint8_t)((gpioab >> 8) & 0x0003), (uint8_t)((hardwareRegister >> 8) & 0x00000003));
    // RF
    gpioab = GetRFMCPRegisters();
    EXPECT_EQ((uint8_t)(gpioab & 0x003F), (uint8_t)((hardwareRegister >> TXATTLSB) & 0x0000003F)); // tx atten
    EXPECT_EQ((uint8_t)((gpioab >> 8) & 0x003F), (uint8_t)((hardwareRegister >> RXATTLSB) & 0x0000003F));
    // BPF
    gpioab = GetBPFMCPRegisters();
    EXPECT_EQ(gpioab, BPF_WORD);
    // Teensy
    EXPECT_EQ(digitalRead(RXTX),GET_BIT(hardwareRegister,RXTXBIT));
    EXPECT_EQ(digitalRead(CW_ON_OFF),GET_BIT(hardwareRegister,CWBIT));
    EXPECT_EQ(digitalRead(XMIT_MODE),GET_BIT(hardwareRegister,MODEBIT));
    EXPECT_EQ(digitalRead(CAL),GET_BIT(hardwareRegister,CALBIT));
}

void CheckThatStateIsReceive(){
    // Check that the hardware register contains the expected bits
    int32_t band = ED.currentBand[ED.activeVFO];
    EXPECT_EQ(GETHWRBITS(LPFBAND0BIT,4), BandToBCD(band)); // LPF filter
    EXPECT_EQ(GETHWRBITS(ANT0BIT,2), ED.antennaSelection[band]); // antenna
    EXPECT_EQ(GET_BIT(hardwareRegister,XVTRBIT), 0);   // transverter should be LO (in path) for receive
    EXPECT_EQ(GET_BIT(hardwareRegister,PA100WBIT), 0); // PA should always be LO (bypassed)
    EXPECT_EQ(GET_BIT(hardwareRegister,TXBPFBIT), 0);  // TX path should bypass BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXBPFBIT), 1);  // RX path should include BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXTXBIT), 0);      // RXTX bit should be RX (0)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWBIT), 0);     // CW bit should be 0 (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,MODEBIT), 1);   // MODE doesn't matter for receive, should be HI(SSB)
    EXPECT_EQ(GET_BIT(hardwareRegister,CALBIT), 0);    // Cal should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWVFOBIT), 0);  // CW transmit VFO should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,SSBVFOBIT), 1); // SSB VFO should be HI (on)    
    EXPECT_EQ(GETHWRBITS(TXATTLSB,6), (uint8_t)round(2*ED.XAttenSSB[band])); // TX attenuation
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), (uint8_t)round(2*ED.RAtten[band]));  // RX attenuation
    EXPECT_EQ(GETHWRBITS(BPFBAND0BIT,4), BandToBCD(band)); // BPF filter
    // Now check that the GPIO registers match the hardware register
    CheckThatHardwareRegisterMatchesActualHardware();
}

void CheckThatStateIsSSBTransmit(){
    int32_t band = ED.currentBand[ED.activeVFO];
    EXPECT_EQ(GETHWRBITS(LPFBAND0BIT,4), BandToBCD(band)); // LPF filter
    EXPECT_EQ(GETHWRBITS(ANT0BIT,2), ED.antennaSelection[band]); // antenna
    EXPECT_EQ(GET_BIT(hardwareRegister,XVTRBIT), 1);   // transverter should be HI (bypass) for transmit
    EXPECT_EQ(GET_BIT(hardwareRegister,PA100WBIT), 0); // PA should always be LO (bypassed)
    EXPECT_EQ(GET_BIT(hardwareRegister,TXBPFBIT), 1);  // TX path should include BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXBPFBIT), 0);  // RX path should bypass BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXTXBIT), 1);   // RXTX bit should be TX (1)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWBIT), 0);     // CW bit should be 0 (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,MODEBIT), 1);   // MODE should be HI(SSB)
    EXPECT_EQ(GET_BIT(hardwareRegister,CALBIT), 0);    // Cal should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWVFOBIT), 0);  // CW transmit VFO should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,SSBVFOBIT), 1); // SSB VFO should be HI (on)    
    EXPECT_EQ(GETHWRBITS(TXATTLSB,6), (uint8_t)round(2*ED.XAttenSSB[band])); // TX attenuation
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), (uint8_t)round(2*ED.RAtten[band]));  // RX attenuation
    EXPECT_EQ(GETHWRBITS(BPFBAND0BIT,4), BandToBCD(band)); // BPF filter
    CheckThatHardwareRegisterMatchesActualHardware();
}

void CheckThatStateIsCWTransmitMark(){
    int32_t band = ED.currentBand[ED.activeVFO];
    EXPECT_EQ(GETHWRBITS(LPFBAND0BIT,4), BandToBCD(band)); // LPF filter
    EXPECT_EQ(GETHWRBITS(ANT0BIT,2), ED.antennaSelection[band]); // antenna
    EXPECT_EQ(GET_BIT(hardwareRegister,XVTRBIT), 1);   // transverter should be HI (bypass) for transmit
    EXPECT_EQ(GET_BIT(hardwareRegister,PA100WBIT), 0); // PA should always be LO (bypassed)
    EXPECT_EQ(GET_BIT(hardwareRegister,TXBPFBIT), 1);  // TX path should include BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXBPFBIT), 0);  // RX path should bypass BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXTXBIT), 1);   // RXTX bit should be TX (1)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWBIT), 1);     // CW bit should be 1 (on)
    EXPECT_EQ(GET_BIT(hardwareRegister,MODEBIT), 0);   // MODE should be LO (CW)
    EXPECT_EQ(GET_BIT(hardwareRegister,CALBIT), 0);    // Cal should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWVFOBIT), 1);  // CW transmit VFO should be 1 (on)
    EXPECT_EQ(GET_BIT(hardwareRegister,SSBVFOBIT), 0); // SSB VFO should be LO (off) 
    EXPECT_EQ(GETHWRBITS(TXATTLSB,6), (uint8_t)round(2*ED.XAttenSSB[band])); // TX attenuation
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), (uint8_t)round(2*ED.RAtten[band]));  // RX attenuation
    EXPECT_EQ(GETHWRBITS(BPFBAND0BIT,4), BandToBCD(band)); // BPF filter
    CheckThatHardwareRegisterMatchesActualHardware();
}

void CheckThatStateIsCWTransmitSpace(){
    int32_t band = ED.currentBand[ED.activeVFO];
    EXPECT_EQ(GETHWRBITS(LPFBAND0BIT,4), BandToBCD(band)); // LPF filter
    EXPECT_EQ(GETHWRBITS(ANT0BIT,2), ED.antennaSelection[band]); // antenna
    EXPECT_EQ(GET_BIT(hardwareRegister,XVTRBIT), 1);   // transverter should be HI (bypass) for transmit
    EXPECT_EQ(GET_BIT(hardwareRegister,PA100WBIT), 0); // PA should always be LO (bypassed)
    EXPECT_EQ(GET_BIT(hardwareRegister,TXBPFBIT), 1);  // TX path should include BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXBPFBIT), 0);  // RX path should bypass BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXTXBIT), 1);   // RXTX bit should be TX (1)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWBIT), 0);     // CW bit should be 0 (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,MODEBIT), 0);   // MODE should be LO (CW)
    EXPECT_EQ(GET_BIT(hardwareRegister,CALBIT), 0);    // Cal should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWVFOBIT), 1);  // CW transmit VFO should be 1 (on)
    EXPECT_EQ(GET_BIT(hardwareRegister,SSBVFOBIT), 0); // SSB VFO should be LO (off) 
    EXPECT_EQ(GETHWRBITS(TXATTLSB,6), (uint8_t)round(2*ED.XAttenSSB[band])); // TX attenuation
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), (uint8_t)round(2*ED.RAtten[band]));  // RX attenuation
    EXPECT_EQ(GETHWRBITS(BPFBAND0BIT,4), BandToBCD(band)); // BPF filter
    CheckThatHardwareRegisterMatchesActualHardware();
}


void print_frequency_state(void){
    Debug("| VFO freq [Hz] | Fine tune [Hz] | RXTX freq [Hz] | SSB VFO [Hz] | CW VFO [Hz] |");
    Debug("|---------------|----------------|----------------|--------------|-------------|");
    String line = "| ";
    line += String(ED.centerFreq_Hz[ED.activeVFO]);
    while (line.length() < 15) line += " ";
    line += " | ";
    line += String(ED.fineTuneFreq_Hz[ED.activeVFO]);
    while (line.length() < 32) line += " ";
    line += " | ";
    line += String(GetTXRXFreq_dHz()/100);
    while (line.length() < 49) line += " ";
    line += " | ";
    line += String(GetSSBVFOFrequency());
    while (line.length() < 64) line += " ";
    line += " | ";
    line += String(GetCWVFOFrequency());
    while (line.length() < 78) line += " ";
    line += " |";
    Debug(line);
}

TEST(Radio, RadioStateRunThrough) {
    // This test goes through the radio startup routine and checks that the state is as we expect

    // Set up the queues so we get some simulated data through and start the "clock"
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    StartMillis();

    //-------------------------------------------------------------
    // Radio startup code
    //-------------------------------------------------------------

    // Initialize the hardware
    InitializeFrontPanel();
    InitializeSignalProcessing();
    InitializeAudio();
    InitializeDisplay();
    InitializeRFHardware(); // RF board, LPF board, and BPF board

    // Start the mode state machines
    modeSM.vars.waitDuration_ms = CW_TRANSMIT_SPACE_TIMEOUT_MS;
    modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
    ModeSm_start(&modeSM);
    UISm_start(&uiSM);
    UpdateAudioIOState();

    // Now, start the 1ms timer interrupt to simulate hardware timer
    start_timer1ms();

    //-------------------------------------------------------------
    
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Check the state before loop is invoked and then again after
    CheckThatStateIsReceive();
    for (size_t i = 0; i < 50; i++){
        loop();
        MyDelay(10);
    }
    CheckThatStateIsReceive();

    // Now, press the BAND UP button and check that things changed as expected
    int32_t oldband = ED.currentBand[ED.activeVFO];
    SetButton(BAND_UP);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(ED.currentBand[ED.activeVFO],oldband+1);
    CheckThatStateIsReceive();
    // go back down
    oldband = ED.currentBand[ED.activeVFO];
    SetButton(BAND_DN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(ED.currentBand[ED.activeVFO],oldband-1);
    CheckThatStateIsReceive();

    // Change the fine tune frequency
    Debug("Before fine tune change:");print_frequency_state();
    SetInterrupt(iFINETUNE_INCREASE);
    loop(); MyDelay(10);
    int64_t oldrxtx = GetTXRXFreq_dHz();
    Debug("After fine tune change:");print_frequency_state();

    // Change the zoom level
    Debug("Before zoom change:");
    Debug(ED.spectrum_zoom);
    SetButton(ZOOM);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    Debug("After zoom change:");
    Debug(ED.spectrum_zoom);
    
    // Go to SSB transmit mode
    SetInterrupt(iPTT_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_TRANSMIT);
    CheckThatStateIsSSBTransmit();
    for (size_t i = 0; i < 50; i++){
        loop();
        MyDelay(10);
    }
    CheckThatStateIsSSBTransmit();
    EXPECT_EQ(oldrxtx, GetTXRXFreq_dHz());
    EXPECT_EQ(oldrxtx, GetSSBVFOFrequency()*100);
    Debug("In TX mode:");print_frequency_state();

    // Change frequency while transmitting
    int64_t oldfreq = ED.centerFreq_Hz[ED.activeVFO];
    SetInterrupt(iCENTERTUNE_INCREASE);
    loop(); MyDelay(10);
    CheckThatStateIsSSBTransmit();
    EXPECT_EQ(ED.centerFreq_Hz[ED.activeVFO], oldfreq+ED.freqIncrement);
    EXPECT_EQ(oldrxtx+100*ED.freqIncrement, GetTXRXFreq_dHz());
    int64_t txcentfreq = ED.centerFreq_Hz[ED.activeVFO];
    Debug("After center change:");print_frequency_state();

    // Go back to SSB receive mode
    SetInterrupt(iPTT_RELEASED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    CheckThatStateIsReceive();
    EXPECT_EQ(oldrxtx+100*ED.freqIncrement, GetTXRXFreq_dHz()); // rxtx should stay the same
    Debug("Back to SSB receive mode:");print_frequency_state();
    
    // Switch to CW receive mode
    SetButton(TOGGLE_MODE);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
    CheckThatStateIsReceive();
    EXPECT_EQ(oldrxtx+100*ED.freqIncrement, GetTXRXFreq_dHz()); // rxtx should stay the same
    Debug("Change to CW receive mode:");print_frequency_state();
    
    // Press the key to start transmitting
    SetInterrupt(iKEY1_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_MARK);
    CheckThatStateIsCWTransmitMark();
    Debug("Change to CW transmit mark mode:");print_frequency_state();
    EXPECT_EQ(GetCWVFOFrequency()*100, GetCWTXFreq_dHz());
    for (size_t i = 0; i < 50; i++){
        loop();
        MyDelay(10);
    }
    CheckThatStateIsCWTransmitMark();


    // Do a sequence of key pressed and releases
    SetInterrupt(iKEY1_RELEASED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_SPACE);
    CheckThatStateIsCWTransmitSpace();

    SetInterrupt(iKEY1_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_MARK);
    CheckThatStateIsCWTransmitMark();

    SetInterrupt(iKEY1_RELEASED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_SPACE);
    CheckThatStateIsCWTransmitSpace();

    SetInterrupt(iKEY1_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_MARK);
    CheckThatStateIsCWTransmitMark();


    // Release the PTT key, we should go to receive state after a delay
    SetInterrupt(iKEY1_RELEASED);
    loop();
    // Immediately after key is released we are still in transmit space state
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_SPACE);
    CheckThatStateIsCWTransmitSpace();
    // Then, after at least waitDuration_ms, we should go back to receive
    StartMillis();
    for (size_t i = 0; i < 50; i++){
        loop();
        MyDelay(10);
        if (millis() < CW_TRANSMIT_SPACE_TIMEOUT_MS){
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_SPACE);
        }
    }
    CheckThatStateIsReceive();


    // Change the key type
    ED.keyType = KeyTypeId_Keyer;
    ED.keyerFlip = false;

    // when flip is false, key 1 is a dit
    // flush the hardware register
    StartMillis();
    buffer_flush();
    SetInterrupt(iKEY1_PRESSED);
    loop();
    int64_t m0 = millis();
    for (size_t i = 0; i < 600; i++){
        loop(); MyDelay(1);
        int64_t m = millis();
        
        // Check that the mode state machine is changing as expected
        if (m-m0 < DIT_DURATION_MS-2){
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
            CheckThatStateIsCWTransmitMark();
        }
        if ((m-m0 > DIT_DURATION_MS+5) & (m-m0 < 2*DIT_DURATION_MS)){ // 5ms grace period
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
            CheckThatStateIsCWTransmitSpace();
        }
        if ((m-m0 > 2*DIT_DURATION_MS+10) & (m-m0 < (2*DIT_DURATION_MS+CW_TRANSMIT_SPACE_TIMEOUT_MS+1))){
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT);
            CheckThatStateIsCWTransmitSpace();
        }        
        if (m-m0 > (2*DIT_DURATION_MS+CW_TRANSMIT_SPACE_TIMEOUT_MS+25+150)){ // 25 ms grace + 150 ms hardware change
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
            CheckThatStateIsReceive();
        }
    }
    CheckThatStateIsReceive();

    // Change the key direction
    ED.keyerFlip = true;

    // when flip is true, key 1 is a dah
    // flush the hardware register
    StartMillis();
    buffer_flush();
    SetInterrupt(iKEY1_PRESSED);
    loop();
    m0 = millis();
    for (size_t i = 0; i < 800; i++){
        loop(); MyDelay(1);
        int64_t m = millis();
        
        // Check that the mode state machine is changing as expected
        if (m-m0 < DIT_DURATION_MS*3-2){
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DAH_MARK);
            CheckThatStateIsCWTransmitMark();
        }
        if ((m-m0 > DIT_DURATION_MS*3+15) & (m-m0 < DIT_DURATION_MS*4)){ // 5ms grace period
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
            CheckThatStateIsCWTransmitSpace();
        }
        if ((m-m0 > DIT_DURATION_MS*4+30) & (m-m0 < (DIT_DURATION_MS*4+CW_TRANSMIT_SPACE_TIMEOUT_MS+1))){
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT);
            CheckThatStateIsCWTransmitSpace();
        }        
        if (m-m0 > (DIT_DURATION_MS*4+CW_TRANSMIT_SPACE_TIMEOUT_MS+35+150)){ // 35 ms grace + 150 ms hardware change
            //Debug(String(m-m0));
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
            CheckThatStateIsReceive();
        }
    }
    CheckThatStateIsReceive();
    

    // Now buffer up three commands: dit dit dah
    // flush the hardware register
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
    StartMillis();
    buffer_flush();
    EXPECT_EQ(GetInterruptFifoSize(),0);
    SetInterrupt(iKEY2_PRESSED);
    EXPECT_EQ(GetInterruptFifoSize(),1);
    loop();
    m0 = millis();
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
    EXPECT_EQ(GetInterruptFifoSize(),0);
    SetInterrupt(iKEY2_PRESSED);
    EXPECT_EQ(GetInterruptFifoSize(),1);
    loop();
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
    EXPECT_EQ(GetInterruptFifoSize(),1);
    SetInterrupt(iKEY1_PRESSED);
    EXPECT_EQ(GetInterruptFifoSize(),2);
    loop();
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
    EXPECT_EQ(GetInterruptFifoSize(),2);
    for (size_t i = 0; i < 1000; i++){
        loop(); MyDelay(1);
        int64_t m = millis();
        // m0 is 50 ms
        // Check that the mode state machine is changing as expected
        if (m < m0+DIT_DURATION_MS-2){ // 108
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
            CheckThatStateIsCWTransmitMark();
        }
        // 115 to 170 (50+120)
        if ((m > m0+DIT_DURATION_MS+5) & (m < m0+DIT_DURATION_MS*2)){ // 5ms grace period
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
            CheckThatStateIsCWTransmitSpace();
        }
        // 180 to 240
        if ((m > m0+DIT_DURATION_MS*2+10) & (m < m0+DIT_DURATION_MS*3)){ // 5ms grace period
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
            CheckThatStateIsCWTransmitMark();
        }
        // 255 to 290
        if ((m > m0+DIT_DURATION_MS*3+15) & (m < m0+DIT_DURATION_MS*4)){ // 5ms grace period
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
            CheckThatStateIsCWTransmitSpace();
        }
        // 310 to 470
        if ((m > m0+DIT_DURATION_MS*4+20) & (m < m0+DIT_DURATION_MS*7)){ // 20ms grace period
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DAH_MARK);
            CheckThatStateIsCWTransmitMark();
        }
        // 505 to 530
        if ((m > m0+DIT_DURATION_MS*7+35) & (m < m0+DIT_DURATION_MS*8)){
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
            CheckThatStateIsCWTransmitSpace();
        }
        // 570 to 730
        if ((m > m0+DIT_DURATION_MS*8+40) & (m < m0+DIT_DURATION_MS*8+CW_TRANSMIT_SPACE_TIMEOUT_MS)){
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT);
            CheckThatStateIsCWTransmitSpace();
        }
        // 995 to end
        if (m > m0+(DIT_DURATION_MS*8+CW_TRANSMIT_SPACE_TIMEOUT_MS+50+150)){ // 35 ms grace + 150 ms hardware change
            EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
            CheckThatStateIsReceive();
        }
    }
    CheckThatStateIsReceive();
    
    buffer_pretty_buffer_array();

    //buffer_pretty_print();
    //buffer_pretty_buffer_array();
    //buffer_pretty_print_last_entry();
    //CheckThatStateIsSSBTransmit();

    // Stop the 1ms timer interrupt
    stop_timer1ms();
}
