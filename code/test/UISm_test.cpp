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
