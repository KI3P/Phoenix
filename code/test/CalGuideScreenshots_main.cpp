/**
 * @file CalGuideScreenshots_main.cpp
 * @brief Screenshots for the calibration guide,
 *        code/docs/CalibrationGuide/Calibration_Guide.html
 *
 * Runs the firmware as the radio simulator does (setup(), loop(), the 1 ms
 * timer on its own thread), with the SDL display, and plays the operator
 * through the calibration screens from the menus. The simulator's own test
 * signals stand in for the bench, exactly as in the interactive simulator:
 * - Receive IQ: the RX IQ test tone (RadioSimulator_main.cpp)
 * - Transmit IQ (dual VFO): the transmitter looped back into the receiver
 *
 * A "dual" run captures the screens common to both kinds of radio and the
 * dual-VFO Transmit IQ calibration; a "single" run captures the BIT screen and
 * the Transmit IQ calibration of a single-VFO radio. The automatic power and
 * SWR calibration has its own tool, autocal_screenshots.
 *
 * Build: cmake -DUSE_SDL_DISPLAY=ON .. && make calguide_screenshots
 * Run:   SDL_VIDEODRIVER=offscreen ./calguide_screenshots <single|dual> <output directory>
 * The screenshots are BMP files named <single|dual>-<nn>-<name>.bmp.
 */

#include <cmath>
#include <functional>

#include "SimScript.h"

extern size_t primaryMenuIndex;
extern size_t secondaryMenuIndex;
char *command_parser(char *command);

static std::string outDir;
static std::string runName;
static int shotNumber = 0;

static void Shot(const char *name){
    char path[512];
    snprintf(path, sizeof(path), "%s/%s-%02d-%s.bmp", outDir.c_str(), runName.c_str(), ++shotNumber, name);
    SaveShot(path);
}

static void Fail(const std::string &why){
    std::cerr << "FAILED: " << why << " (UI state " << UISm_state_id_to_string((UISm_StateId)uiSM.state_id)
              << ", mode state " << ModeSm_state_id_to_string((ModeSm_StateId)modeSM.state_id) << ")" << std::endl;
    stop_timer1ms();
    exit(1);
}

static void RunUntil(std::function<bool(void)> pred, const char *what, uint32_t timeout_ms = 900000){
    uint32_t t0 = millis();
    while (!pred()){
        if (millis() - t0 > timeout_ms)
            Fail(std::string("timed out waiting for ") + what);
        Step();
    }
}

///////////////////////////////////////////////////////////////////////////////
// The simulator's test signals, as RadioSimulator_main.cpp routes them
///////////////////////////////////////////////////////////////////////////////

static RFHardwareState previousRFState = RFInvalid;
// Where the single-tone test carrier sits relative to the receive centre
static const int32_t SIM_CARRIER_OFFSET_HZ = 1000;

static void RouteTestSignals(void){
    RFHardwareState state = GetRFHardwareState();
    if (state == previousRFState) return;
    previousRFState = state;
    switch (state){
        case RFCalReceiveIQ:
            Q_in_L_Ex.setOscillatorSource(nullptr);
            Q_in_R_Ex.setOscillatorSource(nullptr);
            // The test tone with the mock's 10 % amplitude error, in the bin
            // the calibration reads as the wanted signal
            setAudioInputSource(AUDIO_SOURCE_RXIQ_USB);
            break;
        case RFCalTransmitIQ:
            Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);
            Q_in_R_Ex.setOscillatorSource(&transmitIQcal_oscillator);
            setAudioInputSource(AUDIO_SOURCE_FEEDBACK);
            break;
        case RFCalTransmitIQSingleVFO:
            // Real transmit into the antenna connector: the receiver sees nothing
            Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);
            Q_in_R_Ex.setOscillatorSource(&transmitIQcal_oscillator);
            break;
        default:
            Q_in_L_Ex.setOscillatorSource(nullptr);
            Q_in_R_Ex.setOscillatorSource(nullptr);
            setAudioInputSource(AUDIO_SOURCE_SINGLE_TONE);
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// Menus
///////////////////////////////////////////////////////////////////////////////

static const size_t MENU_CALIBRATION = 6;
static const size_t MENU_DIAGNOSTICS = 7;
enum CalItem { CAL_SMETER = 0, CAL_FREQUENCY = 1, CAL_RXIQ = 2, CAL_TXIQ = 3, CAL_POWER = 4 };

/** From the home screen: open the main menu and turn to a primary item */
static void OpenPrimary(size_t primary){
    if (uiSM.state_id != UISm_StateId_HOME) Fail("not on the home screen");
    primaryMenuIndex = 0;
    secondaryMenuIndex = 0;
    Press(MAIN_MENU_UP);
    Settle();
    for (size_t i = 0; i < primary; i++) Turn(iFILTER_INCREASE);
    Settle();
}

/** From the primary menu: select it and turn to a secondary item */
static void OpenSecondary(size_t secondary){
    Press(MENU_OPTION_SELECT);
    Settle();
    for (size_t i = 0; i < secondary; i++) Turn(iFILTER_INCREASE);
    Settle();
}

static void GoHome(void){
    Press(HOME_SCREEN);
    Settle(100);
    if (uiSM.state_id != UISm_StateId_HOME) Fail("HOME did not return to the home screen");
}

///////////////////////////////////////////////////////////////////////////////
// The screens
///////////////////////////////////////////////////////////////////////////////

static void HomeAndMenus(void){
    Shot("home");
    OpenPrimary(MENU_CALIBRATION);
    Shot("menu-primary");
    OpenSecondary(CAL_FREQUENCY);
    Shot("menu-calibration");
    GoHome();
}

static void BITScreen(void){
    OpenPrimary(MENU_DIAGNOSTICS);
    OpenSecondary(0);
    Press(MENU_OPTION_SELECT);
    Settle(200);
    Shot("bit");
    GoHome();
}

static void TuneOntoTestCarrier(void){
    int64_t dial = GetTXRXFreq(ED.activeVFO);
    ED.fineTuneFreq_Hz[ED.activeVFO] += SIM_CARRIER_OFFSET_HZ;
    ED.centerFreq_Hz[ED.activeVFO] += SIM_CARRIER_OFFSET_HZ;
    UpdateRFHardwareState();
    Settle(50);
    std::cout << "Dial " << dial << " Hz -> " << GetTXRXFreq(ED.activeVFO) << " Hz" << std::endl;
}

static void FrequencyCalibration(void){
    // Tune to a 10 MHz standard and select SAM before entering
    char cmd[] = "FA00010000000;";
    command_parser(cmd);
    Settle(50);
    // The simulator's test carrier is 1 kHz from the receive centre whatever
    // the dial says: move the fine tune onto it, and the centre tune by the
    // same amount so that the dial still reads 10.000.000
    TuneOntoTestCarrier();
    for (int i = 0; (i < 6) && (ED.modulation[ED.activeVFO] != SAM); i++){
        Press(DEMODULATION);
        Settle(20);
    }
    if (ED.modulation[ED.activeVFO] != SAM) Fail("could not select SAM");
    Settle(200);
    Shot("freq-home-sam");
    OpenPrimary(MENU_CALIBRATION);
    OpenSecondary(CAL_FREQUENCY);
    Press(MENU_OPTION_SELECT);
    Settle(100);
    if (uiSM.state_id != UISm_StateId_CALIBRATE_FREQUENCY) Fail("not in frequency calibration");
    Settle(500);
    Shot("freq-cal");
    GoHome();
}

static void SelectBandOnScreen(int32_t band){
    // Always press at least once: the screen tunes to the band only when a band
    // button is pressed, even if the band number is already right
    int i = 0;
    do {
        Press(BAND_UP);
        Settle(20);
    } while ((++i < NUMBER_OF_BANDS + 1) && (ED.currentBand[ED.activeVFO] != band));
    if (ED.currentBand[ED.activeVFO] != band) Fail("could not select the band");
}

static void ReceiveIQCalibration(void){
    OpenPrimary(MENU_CALIBRATION);
    OpenSecondary(CAL_RXIQ);
    Press(MENU_OPTION_SELECT);
    Settle(100);
    if (uiSM.state_id != UISm_StateId_CALIBRATE_RX_IQ) Fail("not in Receive IQ calibration");
    SelectBandOnScreen(BAND_20M);
    Settle(500);
    Shot("rxiq-entry");
    Press(16);
    RunUntil([](){ return ED.currentBand[ED.activeVFO] == BAND_40M; }, "RX IQ auto run at 40 m");
    Settle(1500);
    Shot("rxiq-auto");
    RunUntil([](){ return !RXIQCalibrationAutoActive(); }, "RX IQ auto run to finish");
    // The run ends on the general coverage band; show an amateur band
    SelectBandOnScreen(BAND_20M);
    Settle(500);
    Shot("rxiq-done");
    GoHome();
}

static void EnterTransmitIQ(void){
    OpenPrimary(MENU_CALIBRATION);
    OpenSecondary(CAL_TXIQ);
    Press(MENU_OPTION_SELECT);
    Settle(100);
    if (uiSM.state_id != UISm_StateId_CALIBRATE_TX_IQ) Fail("not in Transmit IQ calibration");
    Settle(300);
}

static void KeyPTT(bool down){
    SetInterrupt(down ? iPTT_PRESSED : iPTT_RELEASED);
    Step();
}

static void TransmitIQCalibrationDual(void){
    EnterTransmitIQ();
    SelectBandOnScreen(BAND_40M);
    Settle(200);
    Shot("txiq-entry");
    KeyPTT(true);
    if (modeSM.state_id != ModeSm_StateId_CALIBRATE_TX_IQ_MARK) Fail("PTT did not key");
    Settle(1500);
    Shot("txiq-keyed");
    KeyPTT(false);
    Settle(100);
    // The automatic run starts from the band on the screen
    SelectBandOnScreen(FIRST_BAND);
    Press(16);
    RunUntil([](){ return ED.currentBand[ED.activeVFO] == BAND_40M; }, "TX IQ auto run at 40 m");
    Settle(2000);
    Shot("txiq-auto");
    RunUntil([](){ return !TXIQCalibrationAutoActive(); }, "TX IQ auto run to finish");
    SelectBandOnScreen(BAND_20M);
    Settle(500);
    Shot("txiq-done");
    GoHome();
}

static void TransmitIQCalibrationSingle(void){
    EnterTransmitIQ();
    SelectBandOnScreen(BAND_40M);
    Settle(200);
    Shot("txiq-entry");
    // The operator backs the attenuation off from 31.5 dB until the analyser
    // shows the tone clearly
    for (int i = 0; i < 12; i++) Turn(iFINETUNE_DECREASE);
    KeyPTT(true);
    if (modeSM.state_id != ModeSm_StateId_CALIBRATE_TX_IQ_MARK) Fail("PTT did not key");
    Settle(500);
    Shot("txiq-keyed");
    KeyPTT(false);
    Settle(100);
    GoHome();
}

static void ManualPowerCalibration(void){
    // 20 m, where the automatic calibration's screenshots are taken
    while (ED.currentBand[ED.activeVFO] != BAND_20M){
        Press(BAND_UP);
        Settle(20);
    }
    OpenPrimary(MENU_CALIBRATION);
    OpenSecondary(CAL_POWER);
    Press(MENU_OPTION_SELECT);
    Settle(200);
    if (uiSM.state_id != UISm_StateId_CALIBRATE_POWER) Fail("not in power calibration");
    Shot("power-manual");
    // Three points, 6 dB apart, as a wattmeter would read them
    const float32_t watts[3] = {10.96f, 8.13f, 3.27f};
    for (int p = 0; p < 3; p++){
        for (int i = 0; (p > 0) && (i < 12); i++) Turn(iVOLUME_INCREASE);
        SetMeasuredPower(watts[p]);
        Settle(50);
        if (p == 0) Shot("power-point1");
        Press(MENU_OPTION_SELECT);
        Settle(100);
    }
    Shot("power-curve");
    Press(12);
    Settle(100);
    SetMeasuredPower(3.02f);
    Settle(50);
    Shot("power-ssb");
    // Leave without keeping these made-up points
    GoHome();
}

static void SMeterLevel(void){
    OpenPrimary(MENU_CALIBRATION);
    OpenSecondary(CAL_SMETER);
    Press(MENU_OPTION_SELECT);
    Settle(200);
    Shot("smeter");
    Press(MENU_OPTION_SELECT);
    Settle(50);
    GoHome();
}

int main(int argc, char *argv[]){
    if (argc < 3){
        std::cerr << "Usage: " << argv[0] << " <single|dual> <output directory>" << std::endl;
        return 2;
    }
    runName = argv[1];
    outDir = argv[2];
    if ((runName != "single") && (runName != "dual")){
        std::cerr << "The first argument must be single or dual" << std::endl;
        return 2;
    }
    std::filesystem::create_directories(outDir);

    // The transmit IQ outputs, which the feedback test signal loops back
    Q_out_L_Ex.setAudioChannel(2);
    Q_out_R_Ex.setAudioChannel(3);

    SimStartRadio(outDir + "/storage-" + runName);
    SetDualVFOs(runName == "dual");
    simStepHook = RouteTestSignals;
    start_timer1ms();

    RunUntil([](){ return uiSM.state_id == UISm_StateId_HOME; }, "home screen", 20000);
    Settle(300);

    // CALGUIDE_ONLY=<section>[,<section>...] runs only those sections, for
    // checking a change quickly
    const char *only = getenv("CALGUIDE_ONLY");
    auto run = [only](const char *section, void (*fn)(void)){
        if (!only || (("," + std::string(only) + ",").find("," + std::string(section) + ",") != std::string::npos))
            fn();
    };
    if (runName == "dual"){
        run("menus", HomeAndMenus);
        run("bit", BITScreen);
        run("freq", FrequencyCalibration);
        run("rxiq", ReceiveIQCalibration);
        run("txiq", TransmitIQCalibrationDual);
        run("power", ManualPowerCalibration);
        run("smeter", SMeterLevel);
    } else {
        run("bit", BITScreen);
        run("txiq", TransmitIQCalibrationSingle);
    }

    stop_timer1ms();
    RA8875_SDL_Cleanup();
    std::filesystem::remove_all(outDir + "/storage-" + runName);
    return 0;
}
