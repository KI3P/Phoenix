/**
 * @file AutoCalScreenshots_main.cpp
 * @brief Screenshots of the automatic power and SWR calibration, for
 *        code/docs/PowerCalibration/AutoCalibration.md
 *
 * Runs the firmware as the radio simulator does (setup(), loop(), the 1 ms
 * timer on its own thread), with the SDL display, and plays the operator
 * through one level 1 or level 2 session from the menus. A simulated bench
 * answers the AD7991 bridge readings as radio 2 measured on 2026-11-02:
 * - the PA's output against the CW attenuation is radio 2's level 2 sweep
 *   (code/tools/swr/pa_curve_radio2_level2_2026-11-02.csv);
 * - the bridge lines, leaks, the power into the open and short, the SSB tone's
 *   transfer and the 10 m oscillation into the short are the RADIO2 profile of
 *   PowerAutoCalibration_E2E_test.cpp (code/tools/swr bench sheets).
 * The wattmeter readings the operator enters are the bench's true power.
 *
 * 20 m, the band the screenshots show, runs with the firmware's own timing;
 * the other bands are shortened so a session takes about two minutes.
 *
 * Build: cmake -DUSE_SDL_DISPLAY=ON .. && make autocal_screenshots
 * Run:   SDL_VIDEODRIVER=offscreen ./autocal_screenshots <1|2> <output directory>
 * The screenshots are BMP files named L<level>-<nn>-<name>.bmp. The guide uses
 * them as PNGs named Auto-L<level>-<nn>-<name>.png.
 */

#include <iostream>
#include <chrono>
#include <atomic>
#include <thread>
#include <string>
#include <cmath>
#include <functional>
#include <filesystem>

#include "SDT.h"
#include "RA8875.h"
#include "LittleFS_mock.h"
#include "Adafruit_I2CDevice.h"

#define GETHWRBITS(LSB,len) ((hardwareRegister >> LSB) & ((1 << len) - 1))

void setup(void);
void tick1ms(void);
extern void StartMillis(void);
extern LittleFS_Program myfs;
extern RA8875 tft;
extern size_t primaryMenuIndex;
extern size_t secondaryMenuIndex;

///////////////////////////////////////////////////////////////////////////////
// The 1 ms timer, locked to the wall clock
///////////////////////////////////////////////////////////////////////////////

static std::atomic<bool> timer_running{false};
static std::thread timer_thread;

static void start_timer1ms() {
    timer_running.store(true);
    timer_thread = std::thread([]() {
        int64_t last = millis();
        while (timer_running.load()) {
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
    timer_running.store(false);
    if (timer_thread.joinable())
        timer_thread.join();
}

///////////////////////////////////////////////////////////////////////////////
// The bench: radio 2, and the standard on the connector
///////////////////////////////////////////////////////////////////////////////

enum Connector { CONNECT_LOAD, CONNECT_OPEN, CONNECT_SHORT };
static Connector connector = CONNECT_LOAD;
static uint32_t conversions = 0;

static const uint8_t RADIO2_TURNS = 20;

// Radio 2's PA output in mW at each sweep attenuation, 160 m to 6 m
static const float32_t sweepAtt[12] = {0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 31.5f};
static const float32_t paCurve_mW[11][12] = {
    {13176.8f, 12145.5f, 11540.7f, 10976.6f, 10031.1f, 8937.6f, 7130.9f, 4142.3f, 2166.4f, 1166.4f, 567.0f, 443.9f}, // 160 m
    {17779.7f, 16862.5f, 16383.3f, 15239.6f, 13101.2f, 11174.3f, 7054.7f, 4023.8f, 2078.1f, 1127.5f, 551.5f, 425.9f}, // 80 m
    {17381.6f, 16243.4f, 15876.5f, 15525.4f, 13821.3f, 11172.0f, 7470.1f, 4360.2f, 2263.8f, 1220.8f, 595.9f, 459.6f}, // 60 m
    {16983.5f, 16188.7f, 15836.3f, 15248.9f, 12428.2f, 8829.6f, 5085.1f, 2814.6f, 1555.8f, 805.9f, 424.4f, 306.3f}, // 40 m
    {12021.2f, 11641.5f, 11224.4f, 10145.4f, 8672.9f, 5009.9f, 2887.0f, 1576.1f, 854.5f, 441.4f, 231.6f, 161.6f}, // 30 m
    {10961.8f, 10425.5f, 9999.2f, 9033.6f, 8127.2f, 5581.8f, 3265.0f, 1781.4f, 932.9f, 461.9f, 244.8f, 183.3f}, // 20 m
    {7409.1f, 6644.7f, 6556.3f, 6340.4f, 5268.6f, 3854.4f, 2218.8f, 1216.5f, 626.6f, 341.2f, 176.5f, 128.0f}, // 17 m
    {8129.9f, 8129.9f, 8098.2f, 7650.0f, 6113.8f, 3852.9f, 2132.3f, 1200.8f, 620.9f, 335.8f, 172.6f, 124.6f}, // 15 m
    {3546.5f, 3546.5f, 3546.5f, 3546.5f, 3344.9f, 2447.4f, 1333.1f, 718.6f, 380.3f, 203.4f, 109.9f, 78.8f}, // 12 m
    {4901.6f, 4853.3f, 4901.6f, 4901.6f, 4901.6f, 4078.1f, 2509.8f, 1385.5f, 752.7f, 395.3f, 207.1f, 149.4f}, // 10 m
    {1866.4f, 1755.0f, 1268.0f, 886.2f, 701.8f, 437.9f, 224.2f, 117.2f, 51.3f, 24.6f, 11.6f, 7.9f}, // 6 m
};

// The RADIO2 profile of PowerAutoCalibration_E2E_test.cpp, at 160, 40, 20 and 10 m
static const float32_t r2_m_f[4] = {24.79f, 24.62f, 24.99f, 25.13f};
static const float32_t r2_o_f[4] = {1.93f, 1.89f, 1.75f, 1.93f};
static const float32_t r2_m_r[4] = {24.82f, 24.54f, 24.96f, 25.13f};
static const float32_t r2_o_r[4] = {1.90f, 1.66f, 1.68f, 1.56f};
static const float32_t r2_gap[4] = {-0.12f, -0.18f, -0.38f, 0.0f};
static const float32_t r2_transfer[4] = {0.956f, 0.952f, 0.948f, 0.907f};
static const float32_t r2_open_dB[4] = {-3.14f, -2.54f, 4.93f, -0.64f};
static const float32_t r2_short_dB[4] = {3.72f, 0.25f, -3.80f, 0.0f};
static const float32_t r2_S50[4] = {1.085f, 1.083f, 1.126f, 1.198f};

/** A profile value at any band with an LPF: log-frequency interpolation */
static float32_t AtBand(const float32_t v[4], int32_t band){
    static const float32_t f[4] = {1.9f, 7.1f, 14.2f, 28.5f};
    float32_t x = SWRCalBandFrequency_MHz(band);
    if (x >= f[3]) return v[3];
    int i = 0;
    while ((i < 2) && (x > f[i + 1])) i++;
    float32_t t = (log10f(x) - log10f(f[i])) / (log10f(f[i + 1]) - log10f(f[i]));
    return v[i] + (v[i + 1] - v[i]) * t;
}

/** Radio 2's CW output at an attenuation, interpolated in dB */
static float32_t PACurve_dBm(int32_t band, float32_t att){
    const float32_t *p = paCurve_mW[band];
    int i = 0;
    while ((i < 10) && (att > sweepAtt[i + 1])) i++;
    float32_t t = (att - sweepAtt[i]) / (sweepAtt[i + 1] - sweepAtt[i]);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float32_t a = 10.0f * log10f(p[i]), b = 10.0f * log10f(p[i + 1]);
    return a + (b - a) * t;
}

// The SSB tone's DSP gain that gives 3 W into 50 Ohm (as the E2E bench)
static float32_t SSBGainFor3W(int32_t b){ return 0.5f + 0.1f * b; }

static int32_t HardwareBand(void){
    for (int32_t b = BAND_160M; b <= BAND_6M; b++)
        if (BandToBCD(b) == GETHWRBITS(LPFBAND0BIT, 4))
            return b;
    return -1;
}

static bool Transmitting(void){
    return GET_BIT(hardwareRegister, RXTXBIT) &&
           (GET_BIT(hardwareRegister, CWBIT) || GET_BIT(hardwareRegister, MODEBIT));
}

/** True forward power on the line, in dBm */
static float32_t ForwardPower_dBm(void){
    int32_t b = HardwareBand();
    if (b < 0) return -30.0f;
    if (GET_BIT(hardwareRegister, CWBIT))
        return PACurve_dBm(b, GETHWRBITS(TXATTLSB, 6) / 2.0f);
    float32_t gain_dB;
    if (!GetSSBGainOverride(&gain_dB)){
        bool PAsel;
        gain_dB = ED.PowerCal_20W_DSP_Gain_correction_dB[b] + CalculateSSBTXGain(ED.powerOutSSB[b], &PAsel);
    }
    float32_t P = 10.0f * log10f(3000.0f) + AtBand(r2_transfer, b) * (gain_dB - SSBGainFor3W(b));
    if (connector == CONNECT_OPEN) P += AtBand(r2_open_dB, b);
    if (connector == CONNECT_SHORT) P += AtBand(r2_short_dB, b);
    return P;
}

/** The AD7991's answer: channel 0 forward, 1 reflected; counts = mV */
static uint16_t BridgeADC(uint8_t channel){
    if (!Transmitting())
        return channel == 0 ? 317 : 370;   // radio 2's key-up floor
    int32_t b = HardwareBand();
    if (b < 0) return 0;
    float32_t dither = 2.5f * sinf(0.7f * (float32_t)(conversions++));
    // Radio 2's PA oscillates into a 10 m short, whatever the drive
    if ((b == BAND_10M) && (connector == CONNECT_SHORT))
        return (uint16_t)lroundf(1930.0f + dither);
    float32_t Pf = ForwardPower_dBm();
    float32_t Pr = Pf;
    switch (connector){
        case CONNECT_LOAD:{
            float32_t S = AtBand(r2_S50, b);
            Pr += 20.0f * log10f((S - 1.0f) / (S + 1.0f));
            break;
        }
        case CONNECT_OPEN:  Pr -= AtBand(r2_gap, b) / 2.0f; break;
        case CONNECT_SHORT: Pr += AtBand(r2_gap, b) / 2.0f; break;
    }
    float32_t b_f = AtBand(r2_o_f, b) - SWRCAL_OFFSET_CONSTANT_DB;
    float32_t b_r = AtBand(r2_o_r, b) - SWRCAL_OFFSET_CONSTANT_DB;
    float32_t V = (channel == 0) ? AtBand(r2_m_f, b) * (Pf - b_f) : AtBand(r2_m_r, b) * (Pr - b_r);
    V += dither;
    if (V < 0) V = 0;
    if (V > 4095) V = 4095;
    return (uint16_t)lroundf(V);
}

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

static void Step(void){
    SetTiming();
    loop();
    tft.updateScreen();
    MyDelay(1);
}

static void Settle(int loops = 40){
    for (int i = 0; i < loops; i++) Step();
}

static void Press(int32_t button){
    SetButton(button);
    SetInterrupt(iBUTTON_PRESSED);
    Step();
}

static void Turn(InterruptType encoder){
    SetInterrupt(encoder);
    Step();
}

static void Shot(const char *name){
    Settle(20);  // let the panes redraw
    char path[512];
    snprintf(path, sizeof(path), "%s/L%d-%02d-%s.bmp", outDir.c_str(), level, ++shotNumber, name);
    if (RA8875_SDL_SaveScreenshot(path))
        std::cout << "Saved " << path << std::endl;
    else
        std::cerr << "Could not save " << path << std::endl;
}

static void Fail(const std::string &why){
    std::cerr << "FAILED: " << why << " (state " << PowerCalSm_state_id_to_string((PowerCalSm_StateId)powerSM.state_id)
              << ")" << std::endl;
    stop_timer1ms();
    exit(1);
}

/** The wattmeter reading as the operator would dial it in */
static float32_t MeterReading_W(void){
    float32_t W = powf(10.0f, ForwardPower_dBm() / 10.0f) / 1000.0f;
    return roundf(W * 100.0f) / 100.0f;
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
            Transmitting() && (autoCal.band != SHOWN_BAND)){
            Turn(iFILTER_INCREASE);
            SetMeasuredPower(MeterReading_W());
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
                          !autoCal.servicePending && Transmitting(); }, name);
    Settle(30);
    // Turn the filter encoder up to the reading, at the 0.01 W step
    float32_t target = MeterReading_W();
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
    connector = CONNECT_LOAD;
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
    connector = CONNECT_OPEN;
    Press(MENU_OPTION_SELECT);
    RunUntil([](){ return InState(PowerCalSm_StateId_COLLECT) && (autoCal.band == SHOWN_BAND) &&
                          (autoCal.stage == AUTO_STAGE_OPEN) && (autoCal.point == 2) && !autoCal.servicePending; },
             "20 m open sweep");
    Settle(60);
    Shot("open-measuring");

    // The short
    RunUntil([](){ return InState(PowerCalSm_StateId_PROMPT); }, "prompt for the short");
    Shot("prompt-short");
    connector = CONNECT_SHORT;
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
    std::string store = outDir + "/storage";
    std::filesystem::remove_all(store);
    std::filesystem::create_directories(store);
    myfs.setDiskBackingPath(store);

    setup();
    digitalWrite(KEY1, 1);
    digitalWrite(KEY2, 1);
    Adafruit_I2CDevice::setMockADCProvider(BridgeADC);
    start_timer1ms();

    RunSession();

    stop_timer1ms();
    Adafruit_I2CDevice::setMockADCProvider(nullptr);
    RA8875_SDL_Cleanup();
    std::filesystem::remove_all(store);
    return 0;
}
