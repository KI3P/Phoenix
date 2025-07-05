#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"

// Enter the Splash state upon initialization
TEST(UISm, EnterSplashUponInitialization){
    UISm_start(&uiSM);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_SPLASH);
} 

// Transition to Home screen after the required amount of time
TEST(UISm, TransitionFromSplashToHome){
    UISm_start(&uiSM);
    uiSM.vars.splashDuration_ms = SPLASH_DURATION_MS;
    EXPECT_EQ(uiSM.state_id, UISm_StateId_SPLASH);
    for (size_t i = 0; i < SPLASH_DURATION_MS-1; i++){
        UISm_dispatch_event(&uiSM, UISm_EventId_DO);
        EXPECT_EQ(uiSM.state_id, UISm_StateId_SPLASH);
    }
    UISm_dispatch_event(&uiSM, UISm_EventId_DO);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
} 

// Enter menu screen upon menu press
TEST(UISm, TransitionFromHomeToMainMenu){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_HOME;
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_MAIN_MENU_RF);
    UISm_dispatch_event(&uiSM, UISm_EventId_DO);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_MAIN_MENU_RF);
}

// Increment main menu selection upon MENU_INC event
TEST(UISm, IncrementMainMenuSelection){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_MAIN_MENU_RF;
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU_INC);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_MAIN_MENU_AUDIO);
}

// Decrement main menu selection upon MENU_DEC event, with underflow
TEST(UISm, DecrementMainMenuSelection){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_MAIN_MENU_RF;
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU_DEC);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_MAIN_MENU_CANCEL);
}

TEST(UISm, NavigateFromMainToHome){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_MAIN_MENU_CANCEL;
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
}

TEST(UISm, NavigateFromMainToRFMenu){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_MAIN_MENU_RF;
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_RF_ADJUST_RX_GAIN);
}

TEST(UISm, RFMenuIncAndDec){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_RF_ADJUST_RX_GAIN;
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU_INC);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_RF_ADJUST_TX_GAIN);
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU_DEC);
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU_DEC);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_RF_CANCEL);

}

TEST(UISm, RFMenuCancel){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_RF_CANCEL;
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
}

// Enter adjust state from RF_ADJUST_RX_GAIN properly configured
TEST(UISm, RXGainToAdjustTransition){
    UISm_start(&uiSM);
    SetRXAttenuator(20);
    uiSM.state_id = UISm_StateId_RF_ADJUST_RX_GAIN;
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_SHOW_VALUE);
    UIValueUpdateFloat *foo = (UIValueUpdateFloat *)uiSM.vars.uiUp;
    EXPECT_EQ(foo->getValueFunction(),20);
}

// Exit from adjust state
TEST(UISm, AdjustToHomeTransition){
    UISm_start(&uiSM);
    SetRXAttenuator(20);
    uiSM.state_id = UISm_StateId_RF_ADJUST_RX_GAIN;
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_SHOW_VALUE);
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);    
}

// Adjust RX_GAIN
TEST(UISm, RXGainIncrementAndDecrement){
    UISm_start(&uiSM);
    SetRXAttenuator(20);
    uiSM.state_id = UISm_StateId_RF_ADJUST_RX_GAIN;
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU_INC);
    UIValueUpdateFloat *foo = (UIValueUpdateFloat *)uiSM.vars.uiUp;
    EXPECT_EQ(foo->getValueFunction(),20.5);
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU_DEC);
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU_DEC);
    EXPECT_EQ(foo->getValueFunction(),19.5);
}

// Adjust RX_GAIN past the max value via the menu
TEST(UISm, RXGainIncreasePastMax){
    UISm_start(&uiSM);
    SetRXAttenuator(63);
    uiSM.state_id = UISm_StateId_RF_ADJUST_RX_GAIN;
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU_INC);
    UIValueUpdateFloat *foo = (UIValueUpdateFloat *)uiSM.vars.uiUp;
    EXPECT_EQ(foo->getValueFunction(),31.5);
}

// Enter adjust state from RF_ADJUST_TX_GAIN properly configured
TEST(UISm, TXGainToAdjustTransition){
    UISm_start(&uiSM);
    SetRXAttenuator(20);
    SetTXAttenuator(30);
    uiSM.state_id = UISm_StateId_RF_ADJUST_TX_GAIN;
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_SHOW_VALUE);
    UIValueUpdateFloat *foo = (UIValueUpdateFloat *)uiSM.vars.uiUp;
    EXPECT_EQ(foo->getValueFunction(),30);
}

// Adjusting TX Gain doesn't affect RX Gain
TEST(UISm, TXGainAdjustDoesNotAffectRXGain){
    UISm_start(&uiSM);
    SetRXAttenuator(20);
    SetTXAttenuator(30);
    uiSM.state_id = UISm_StateId_RF_ADJUST_TX_GAIN;
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_SHOW_VALUE);
    UIValueUpdateFloat *foo = (UIValueUpdateFloat *)uiSM.vars.uiUp;
    EXPECT_EQ(foo->getValueFunction(),30);
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU_INC);
    EXPECT_EQ(foo->getValueFunction(),30.5);
    EXPECT_EQ(GetRXAttenuator(),20);
}

// Adjust an int32_t
TEST(UISm, AdjustInt){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_RF_ADJUST_SCALE;
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    UIValueUpdateInt *foo = (UIValueUpdateInt *)uiSM.vars.uiUp;
    foo->incrementValue = 5;
    for (uint32_t val = -2; val < 3; val++){
        foo->setValueFunction(val);
        EXPECT_EQ(foo->getValueFunction(),val);
        UISm_dispatch_event(&uiSM, UISm_EventId_MENU_INC);
        EXPECT_EQ(foo->getValueFunction(),val+foo->incrementValue);
        UISm_dispatch_event(&uiSM, UISm_EventId_MENU_DEC);
        UISm_dispatch_event(&uiSM, UISm_EventId_MENU_DEC);
        EXPECT_EQ(foo->getValueFunction(),val-foo->incrementValue);
    }

}

// Increment all the way around the main menu
TEST(UISm, IncrementAroundMainMenu){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_HOME;
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU);
    UISm_StateId state = uiSM.state_id;
    int k = 0;
    for (size_t i = 0; i<30; i++){
        k++;
        UISm_dispatch_event(&uiSM, UISm_EventId_MENU_INC);
        if (uiSM.state_id == state) break;
    }
    EXPECT_GT(k,0);
    EXPECT_LT(k,20);

    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_HOME;
    UISm_dispatch_event(&uiSM, UISm_EventId_MENU);
    state = uiSM.state_id;
    int m = 0;
    for (size_t i = 0; i<30; i++){
        m++;
        UISm_dispatch_event(&uiSM, UISm_EventId_MENU_DEC);
        if (uiSM.state_id == state) break;
    }
    EXPECT_GT(m,0);
    EXPECT_LT(m,20);
    EXPECT_EQ(k,m);
}

// Selecting the calibration options enters the right state and triggers the right actions
TEST(UISm, EnteringCalibrateFreqChangesStateAndTriggersAction){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    uiSM.state_id = UISm_StateId_CALIBRATE_FREQUENCY;
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_FREQUENCY_DISPLAY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_FREQUENCY);
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
}

TEST(UISm, EnteringCalibrateReceiveIQChangesStateAndTriggersAction){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    uiSM.state_id = UISm_StateId_CALIBRATE_RX_IQ;
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_RX_IQ_DISPLAY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_RX_IQ);
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
}

TEST(UISm, EnteringCalibrateTransmitIQChangesStateAndTriggersAction){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    uiSM.state_id = UISm_StateId_CALIBRATE_TX_IQ;
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_TX_IQ_DISPLAY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ);
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
}

TEST(UISm, EnteringCalibrateCWPAChangesStateAndTriggersAction){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    uiSM.state_id = UISm_StateId_CALIBRATE_CW_PA;
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_CW_PA_DISPLAY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_CW_PA);
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
}

TEST(UISm, EnteringCalibrateSSBPAChangesStateAndTriggersAction){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    uiSM.state_id = UISm_StateId_CALIBRATE_SSB_PA;
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_SSB_PA_DISPLAY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_SSB_PA);
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
}

