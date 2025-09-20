#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"

#define GETHWRBITS(start,len) ((hardwareRegister >> start) & ((1 << len) - 1))

void CheckThatStateIsReceive(){
    int32_t band = ED.currentBand[ED.activeVFO];
    EXPECT_EQ(GETHWRBITS(LPFBAND0BIT,4), BandToBCD(band)); // LPF filter
    EXPECT_EQ(GETHWRBITS(ANT0BIT,2), ED.antennaSelection[band]); // antenna
    EXPECT_EQ(GET_BIT(hardwareRegister,XVTRBIT), 0);   // transverter should be LO (in path) for receive
    EXPECT_EQ(GET_BIT(hardwareRegister,PA100WBIT), 0); // PA should always be LO (bypassed)
    EXPECT_EQ(GET_BIT(hardwareRegister,TXBPFBIT), 0);  // TX path should bypass BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXBPFBIT), 1);  // RX path should include BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXTX), 0);      // RXTX bit should be RX (0)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWBIT), 0);     // CW bit should be 0 (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,MODEBIT), 1);   // MODE doesn't matter for receive, should be HI(SSB)
    EXPECT_EQ(GET_BIT(hardwareRegister,CALBIT), 0);    // Cal should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWVFOBIT), 0);  // CW transmit VFO should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,SSBVFOBIT), 1); // SSB VFO should be HI (on)    
    EXPECT_EQ(GETHWRBITS(TXATTLSB,6), (uint8_t)round(2*ED.XAttenSSB[band])); // TX attenuation
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), (uint8_t)round(2*ED.RAtten[band]));  // RX attenuation
    EXPECT_EQ(GETHWRBITS(BPFBAND0BIT,4), BandToBCD(band)); // BPF filter
}

TEST(Radio, StartupInCorrectState) {
    // This test goes through the radio startup routine and checks that the state is as we expect

    // Set up the queues so we get some simulated data through
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();

    // Start the mode state machines
    ModeSm_start(&modeSM);
    UISm_start(&uiSM);
    
    // Initialize the hardware
    FrontPanelInit();
    SetupAudio();
    UpdateAudioIOState();
    InitializeRFHardware(); // RF board, LPF board, and BPF board
    InitializeSignalProcessing();

    // Check the state
    CheckThatStateIsReceive();

    loop();
}