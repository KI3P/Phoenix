/**
 * @file AutoCalScreenshots_main.cpp
 * @brief Screenshots of the automatic power and SWR calibration, for
 *        code/docs/PowerCalibration/AutoCalibration.md
 *
 * Runs the firmware as the radio simulator does (setup(), loop(), the 1 ms
 * timer on its own thread), with the SDL display, and plays the operator
 * through one level 1 or level 2 session from the menus. The simulated bench
 * of SimBench.h answers the AD7991 bridge readings as radio 2 measured on
 * 2026-11-02. The wattmeter readings the operator enters are the bench's true
 * power.
 *
 * 20 m, the band the screenshots show, runs with the firmware's own timing;
 * the other bands are shortened so a session takes about two minutes.
 *
 * Build: cmake -DUSE_SDL_DISPLAY=ON .. && make autocal_screenshots
 * Run:   SDL_VIDEODRIVER=offscreen ./autocal_screenshots <1|2> <output directory>
 * The screenshots are BMP files named L<level>-<nn>-<name>.bmp. The guide uses
 * them as PNGs named Auto-L<level>-<nn>-<name>.png.
 */

#include <cmath>
#include <functional>

#include "SimScript.h"
#include "SimBench.h"

extern size_t primaryMenuIndex;
extern size_t secondaryMenuIndex;

///////////////////////////////////////////////////////////////////////////////
// The operator
///////////////////////////////////////////////////////////////////////////////

static std::string outDir;
static int level = 1;
static int shotNumber = 0;
static const int32_t SHOWN_BAND = BAND_20M;

/** Real timing on the band the screenshots show, shortened elsewhere */
static void SetTiming(void){
    if (!PowerCalAutoActive()) return;
    bool real = (autoCal.band == SHOWN_BAND);
    powerSM.vars.bandSettle_ms = real ? 300 : 20;
    powerSM.vars.settle_ms = real ? 500 : 30;
    powerSM.vars.rest_ms = real ? 1000 : 20;
    autoCal.collectLoad_ms = real ? 1000 : 60;
    autoCal.collectReflection_ms = real ? 1500 : 60;
    autoCal.minReadings = real ? 20 : 2; // 20: AUTO_MIN_READINGS
    // BAND's entry action has already copied the collect time for this band
    powerSM.vars.collect_ms = (autoCal.stage == AUTO_STAGE_LOAD) ? autoCal.collectLoad_ms
                                                                 : autoCal.collectReflection_ms;
}

static void Shot(const char *name){
    char path[512];
    snprintf(path, sizeof(path), "%s/L%d-%02d-%s.bmp", outDir.c_str(), level, ++shotNumber, name);
    SaveShot(path);
}

static void Fail(const std::string &why){
    std::cerr << "FAILED: " << why << " (state " << PowerCalSm_state_id_to_string((PowerCalSm_StateId)powerSM.state_id)
              << ")" << std::endl;
    stop_timer1ms();
    exit(1);
}

/**
 * Run until pred() holds. The operator answers every wattmeter entry except
 * those on the shown band, which the script handles itself.
 */
static void RunUntil(std::function<bool(void)> pred, const char *what, uint32_t timeout_ms = 600000){
    uint32_t t0 = millis();
    while (!pred()){
        if (millis() - t0 > timeout_ms)
            Fail(std::string("timed out waiting for ") + what);
        if ((powerSM.state_id == PowerCalSm_StateId_METER_ENTRY) && !autoCal.servicePending &&
            SimTransmitting() && (autoCal.band != SHOWN_BAND)){
            Turn(iFILTER_INCREASE);
            SetMeasuredPower(SimWattmeter_W());
            Press(MENU_OPTION_SELECT);
        } else {
            Step();
        }
    }
}

static bool InState(PowerCalSm_StateId s){ return powerSM.state_id == s; }

/** A wattmeter entry on the shown band: dial it in, screenshot, SELECT */
static void ShownMeterEntry(const char *name){
    RunUntil([](){ return InState(PowerCalSm_StateId_METER_ENTRY) && (autoCal.band == SHOWN_BAND) &&
                          !autoCal.servicePending && SimTransmitting(); }, name);
    Settle(30);
    // Turn the filter encoder up to the reading, at the 0.01 W step
    float32_t target = SimWattmeter_W();
    int guard = 0;
    while ((fabsf(GetMeasuredPower() - target) > 0.005f) && (guard++ < 2000))
        Turn(GetMeasuredPower() < target ? iFILTER_INCREASE : iFILTER_DECREASE);
    SetMeasuredPower(target); // exact, against float rounding
    std::cout << name << ": wattmeter " << target << " W at attenuation " << autoCal.drive_dB << " dB" << std::endl;
    Shot(name);
    Press(MENU_OPTION_SELECT);
}

static void RunSession(void){
    // Radio startup, to the home screen
    RunUntil([](){ return uiSM.state_id == UISm_StateId_HOME; }, "home screen", 20000);
    Settle(100);

    // Main menu -> Calibration -> Power
    Press(MAIN_MENU_UP);
    Settle();
    for (int i = 0; i < 6; i++) Turn(iFILTER_INCREASE);
    Settle();
    Press(MENU_OPTION_SELECT);
    Settle();
    for (int i = 0; i < 4; i++) Turn(iFILTER_INCREASE);
    if (level == 1) Shot("menu");
    Press(MENU_OPTION_SELECT);
    Settle(100);
    if (uiSM.state_id != UISm_StateId_CALIBRATE_POWER) Fail("not in power calibration");
    if (level == 1) Shot("manual-screen");

    // Button 1: the automatic calibration's setup screen
    Press(1);
    if (!InState(PowerCalSm_StateId_SETUP)) Fail("button 1 did not start AUTO");
    Settle();
    if (level == 1) Shot("setup-default");
    while (autoCal.level > level) Turn(iVOLUME_DECREASE);
    while (autoCal.level < level) Turn(iVOLUME_INCREASE);
    while (ED.SWR_CouplerTurns < RADIO2_TURNS) Turn(iFILTER_INCREASE);
    while (ED.SWR_CouplerTurns > RADIO2_TURNS) Turn(iFILTER_DECREASE);
    // The 0.01 W wattmeter step for the level 2 entries
    if (level == 2) Press(15);
    Shot("setup");

    // The load
    Press(MENU_OPTION_SELECT);
    if (!InState(PowerCalSm_StateId_PROMPT)) Fail("no prompt for the load");
    Shot("prompt-load");
    simConnector = CONNECT_LOAD;
    Press(MENU_OPTION_SELECT);
    RunUntil([](){ return InState(PowerCalSm_StateId_COLLECT) && (autoCal.band == SHOWN_BAND) &&
                          (autoCal.stage == AUTO_STAGE_LOAD) && (autoCal.point == 7) && !autoCal.servicePending; },
             "20 m load sweep");
    Settle(60);
    Shot("load-measuring");
    if (level == 2){
        ShownMeterEntry("meter-2W");
        ShownMeterEntry("meter-full");
    }

    // The open
    RunUntil([](){ return InState(PowerCalSm_StateId_PROMPT); }, "prompt for the open");
    Shot("prompt-open");
    simConnector = CONNECT_OPEN;
    Press(MENU_OPTION_SELECT);
    RunUntil([](){ return InState(PowerCalSm_StateId_COLLECT) && (autoCal.band == SHOWN_BAND) &&
                          (autoCal.stage == AUTO_STAGE_OPEN) && (autoCal.point == 2) && !autoCal.servicePending; },
             "20 m open sweep");
    Settle(60);
    Shot("open-measuring");

    // The short
    RunUntil([](){ return InState(PowerCalSm_StateId_PROMPT); }, "prompt for the short");
    Shot("prompt-short");
    simConnector = CONNECT_SHORT;
    Press(MENU_OPTION_SELECT);
    RunUntil([](){ return InState(PowerCalSm_StateId_REVIEW); }, "review");
    Settle(100);
    Shot("review");

    // Accept
    Press(MENU_OPTION_SELECT);
    RunUntil([](){ return InState(PowerCalSm_StateId_DONE); }, "done");
    Settle(100);
    Shot("done");

    Press(HOME_SCREEN);
    Settle(100);
}

int main(int argc, char *argv[]){
    if (argc < 3){
        std::cerr << "Usage: " << argv[0] << " <level 1|2> <output directory>" << std::endl;
        return 2;
    }
    level = atoi(argv[1]);
    outDir = argv[2];
    if ((level < 1) || (level > 2)){
        std::cerr << "Level must be 1 or 2" << std::endl;
        return 2;
    }
    std::filesystem::create_directories(outDir);

    SimStartRadio(outDir + "/storage");
    simStepHook = SetTiming;
    Adafruit_I2CDevice::setMockADCProvider(SimBridgeADC);
    start_timer1ms();

    RunSession();

    stop_timer1ms();
    Adafruit_I2CDevice::setMockADCProvider(nullptr);
    RA8875_SDL_Cleanup();
    std::filesystem::remove_all(outDir + "/storage");
    return 0;
}
