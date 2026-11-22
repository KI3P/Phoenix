/**
 * @file SimScript.h
 * @brief Helpers for scripted runs of the radio simulator
 *
 * The screenshot tools (autocal_screenshots, calguide_screenshots) run the
 * firmware as the radio simulator does: setup(), then loop() with the 1 ms
 * timer on its own thread and the SDL display. They play an operator by
 * injecting front panel events, and save the display at chosen moments.
 *
 * Include this in one translation unit only (its functions are static).
 */

#ifndef SIMSCRIPT_H
#define SIMSCRIPT_H

#include <iostream>
#include <chrono>
#include <atomic>
#include <thread>
#include <string>
#include <filesystem>

#include "SDT.h"
#include "RA8875.h"
#include "LittleFS_mock.h"

void setup(void);
void tick1ms(void);
extern void StartMillis(void);
extern LittleFS_Program myfs;
extern RA8875 tft;

///////////////////////////////////////////////////////////////////////////////
// The 1 ms timer, locked to the wall clock
///////////////////////////////////////////////////////////////////////////////

static std::atomic<bool> sim_timer_running{false};
static std::thread sim_timer_thread;

static void start_timer1ms() {
    sim_timer_running.store(true);
    sim_timer_thread = std::thread([]() {
        int64_t last = millis();
        while (sim_timer_running.load()) {
            int64_t now = millis();
            if (now < last) last = now;
            while (last < now) {
                tick1ms();
                last++;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });
}

static void stop_timer1ms() {
    sim_timer_running.store(false);
    if (sim_timer_thread.joinable())
        sim_timer_thread.join();
}

///////////////////////////////////////////////////////////////////////////////
// Running the firmware and playing the operator
///////////////////////////////////////////////////////////////////////////////

/** Called before every loop() pass, if set: the script's own bench model */
static void (*simStepHook)(void) = nullptr;

/** One pass of the main loop, a display update and 1 ms of time */
static void Step(void){
    if (simStepHook) simStepHook();
    loop();
    tft.updateScreen();
    MyDelay(1);
}

static void Settle(int loops = 40){
    for (int i = 0; i < loops; i++) Step();
}

/** Press and release a front panel button (IDs as in Config.h) */
static void Press(int32_t button){
    SetButton(button);
    SetInterrupt(iBUTTON_PRESSED);
    Step();
}

/** One detent of an encoder, e.g. iFILTER_INCREASE */
static void Turn(InterruptType encoder){
    SetInterrupt(encoder);
    Step();
}

/** Save the 800x480 display as a BMP */
static bool SaveShot(const std::string &path){
    Settle(20);  // let the panes redraw
    if (RA8875_SDL_SaveScreenshot(path.c_str())){
        std::cout << "Saved " << path << std::endl;
        return true;
    }
    std::cerr << "Could not save " << path << std::endl;
    return false;
}

/**
 * Prepare the audio queues, the clock and a fresh configuration store in
 * <storeDir>, then run the firmware's setup(). Call before start_timer1ms().
 */
static void SimStartRadio(const std::string &storeDir){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_in_L_Ex.setChannel(0);
    Q_in_R_Ex.setChannel(1);
    Q_in_L_Ex.clear();
    Q_in_R_Ex.clear();
    StartMillis();
    // A fresh configuration each run, kept out of the working directory
    std::filesystem::remove_all(storeDir);
    std::filesystem::create_directories(storeDir);
    myfs.setDiskBackingPath(storeDir);

    setup();
    digitalWrite(KEY1, 1);
    digitalWrite(KEY2, 1);
}

#endif // SIMSCRIPT_H
