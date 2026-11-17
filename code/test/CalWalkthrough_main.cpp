/**
 * @file CalWalkthrough_main.cpp
 * @brief Walk through every calibration in the radio simulator and check it
 *
 * Runs the firmware as the radio simulator does (setup(), loop(), the 1 ms
 * timer on its own thread), with the simulated bench of SimBench.h, and plays
 * the operator through each calibration from the menus, in the order of the
 * calibration guide. After each step it checks what the radio did: the
 * readings, the stored settings, the key line, the state machines. Each check
 * prints "ok:" or "FAIL:"; the exit code is the number of failures (2 if a
 * step could not be completed at all).
 *
 * Sections (WALK_ONLY=<section>[,<section>...] runs only those):
 * - freq:    frequency calibration against the simulated crystal error (2.5 ppm);
 *            the TX and CW oscillators are written again afterwards
 * - rxiq:    Receive IQ: band pane, HOME during the automatic run, a run with
 *            the test tone on the wrong sideband, the full automatic run
 * - txiq:    Transmit IQ: dual VFO, HOME while keyed, the automatic run (every
 *            band the radio may transmit on, from 160 m), HOME during it;
 *            single VFO, the attenuation of each key-down
 * - power:   automatic power and SWR calibration, level 1 (load, open, short;
 *            shortened timing), then the power and SWR readings against the
 *            bench's wattmeter
 * - smeter:  the S-meter level offset
 * - carrier: (DIRECT_COUPLED_TX build, cal_walkthrough_dct) the carrier null,
 *            button 14; its CARRCAL performance log goes to carrier-<run>.csv
 *
 * Build: cmake -DUSE_SDL_DISPLAY=ON .. && make cal_walkthrough cal_walkthrough_dct
 * Run:   SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./cal_walkthrough <dual|single> <output directory>
 *        SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./cal_walkthrough_dct <dual|single> <output directory>
 * A full dual run takes about 20 minutes (the automatic IQ runs are real time).
 * Screenshots of the result screens are saved in the output directory.
 *
 * Known to fail (2026-11-16), not fixed: "with no valid reading every band
 * keeps its amplitude" (the IQ sweeps' first amplitude pass reaches 0, where a
 * mirrored tone reads +25 dB).
 */

#include <cmath>
#include <functional>
#include <vector>
#include <fstream>

#include "SimScript.h"
#include "SimBench.h"
#include "RFBoard_si5351.h"

extern size_t primaryMenuIndex;
extern size_t secondaryMenuIndex;
char *command_parser(char *command);
extern void Si5351_Mock_SetDeviceAddress(uint8_t addr);
extern Si5351 si5351;
extern si5351_clock CLK0TX, CLKCW;

static std::string outDir, runName;
static int shotNumber = 0, failures = 0, checks = 0;
static bool forceMirroredRXIQ = false;

#define CHECK(c, msg) do { checks++; if (!(c)) { std::cout << "  FAIL: " << msg << std::endl; failures++; } \
                           else std::cout << "  ok: " << msg << std::endl; } while (0)

static void Shot(const char *name){
    char path[512];
    snprintf(path, sizeof(path), "%s/%s%s-%02d-%s.bmp", outDir.c_str(), runName.c_str(),
#ifdef DIRECT_COUPLED_TX
             "-dct",
#else
             "",
#endif
             ++shotNumber, name);
    SaveShot(path);
}

static void Abort(const std::string &why){
    std::cout << "ABORTED: " << why << " (UI " << UISm_state_id_to_string((UISm_StateId)uiSM.state_id)
              << ", mode " << ModeSm_state_id_to_string((ModeSm_StateId)modeSM.state_id) << ")" << std::endl;
    stop_timer1ms();
    exit(2);
}

static void RunUntil(std::function<bool(void)> pred, const char *what, uint32_t timeout_ms = 1200000){
    uint32_t t0 = millis();
    while (!pred()){
        if (millis() - t0 > timeout_ms) Abort(std::string("timed out waiting for ") + what);
        Step();
    }
}

/** The automatic power calibration's timing, shortened: a session takes a minute or two */
static void ShortPowerCalTiming(void){
    if (!PowerCalAutoActive()) return;
    powerSM.vars.bandSettle_ms = 20;
    powerSM.vars.settle_ms = 30;
    powerSM.vars.rest_ms = 20;
    autoCal.collectLoad_ms = 60;
    autoCal.collectReflection_ms = 60;
    autoCal.minReadings = 2;
    powerSM.vars.collect_ms = (autoCal.stage == AUTO_STAGE_LOAD) ? autoCal.collectLoad_ms
                                                                 : autoCal.collectReflection_ms;
}

/** The bench, before every loop() pass */
static void Bench(void){
    SimRouteTestSignals();
    // A receiver that hears the test tone on the wrong sideband: no setting
    // should give a valid reading
    if (forceMirroredRXIQ && (GetRFHardwareState() == RFCalReceiveIQ) &&
        (getAudioInputSource() != AUDIO_SOURCE_RXIQ_LSB))
        setAudioInputSource(AUDIO_SOURCE_RXIQ_LSB);
    ShortPowerCalTiming();
}

///////////////////////////////////////////////////////////////////////////////
// The operator
///////////////////////////////////////////////////////////////////////////////

static const size_t MENU_CALIBRATION = 6;
enum CalItem { CAL_SMETER = 0, CAL_FREQUENCY = 1, CAL_RXIQ = 2, CAL_TXIQ = 3, CAL_POWER = 4 };

/** From the home screen: Menu, Calibration, the item */
static void OpenCalMenu(size_t item){
    if (uiSM.state_id != UISm_StateId_HOME) Abort("not on the home screen");
    primaryMenuIndex = 0; secondaryMenuIndex = 0;
    Press(MAIN_MENU_UP); Settle();
    for (size_t i = 0; i < MENU_CALIBRATION; i++) Turn(iFILTER_INCREASE);
    Settle(); Press(MENU_OPTION_SELECT); Settle();
    for (size_t i = 0; i < item; i++) Turn(iFILTER_INCREASE);
    Settle();
}

static void OpenCal(size_t item, UISm_StateId expect){
    OpenCalMenu(item);
    Press(MENU_OPTION_SELECT); Settle(100);
    if (uiSM.state_id != expect) Abort("did not open the calibration screen");
}

static void GoHome(void){
    Press(HOME_SCREEN);
    Settle(150);
}

static void SelectBandOnScreen(int32_t band){
    // Always press at least once: the screen tunes to the band only when a
    // band button is pressed
    int i = 0;
    do { Press(BAND_UP); Settle(20); }
    while ((++i < NUMBER_OF_BANDS + 1) && (ED.currentBand[ED.activeVFO] != band));
    if (ED.currentBand[ED.activeVFO] != band) Abort("could not select the band");
}

static void Tune(const char *hz11){
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "FA%s;", hz11);
    command_parser(cmd);
    Settle(50);
}

static void KeyPTT(bool down){ SetInterrupt(down ? iPTT_PRESSED : iPTT_RELEASED); Settle(50); }
static bool Keyed(void){ return GET_BIT(hardwareRegister, RXTXBIT); }
static bool InReceive(void){
    return (uiSM.state_id == UISm_StateId_HOME) && !Keyed() &&
           ((modeSM.state_id == ModeSm_StateId_SSB_RECEIVE) || (modeSM.state_id == ModeSm_StateId_CW_RECEIVE));
}
static bool IsTXBand(int32_t b){ return bands[b].band_type == HAM_BAND; }

///////////////////////////////////////////////////////////////////////////////
// 1. Frequency
///////////////////////////////////////////////////////////////////////////////

/** The CW output: CLK6 on dual-VFO boards, CLK2 on single */
static si5351_clock CWClock(void){ return HasDualVFOs() ? CLKCW : SI5351_CLK2; }

/**
 * Key SSB, then CW, at the dial; check the chip has each transmit oscillator
 * at its cached frequency. A single-VFO board transmits SSB on the receive
 * oscillator, which the correction always restored (and whose mock record
 * does not follow PLL-only retuning), so only its CW output is checked.
 */
static void KeyAndCheckVFOs(const char *when, bool check){
    bool dual = HasDualVFOs();
    KeyPTT(true);
    bool ssb = Keyed() && (GetTXVFOFrequency() != 0) &&
               (si5351.clk_freq[CLK0TX] == (uint64_t)GetTXVFOFrequency() * 100);
    if (dual)
        std::cout << "  SSB keyed " << when << ": TX VFO " << GetTXVFOFrequency() << " Hz, chip "
                  << si5351.clk_freq[CLK0TX] / 100 << " Hz" << std::endl;
    KeyPTT(false);
    Press(TOGGLE_MODE); Settle(50);   // CW
    bool cw = false;
    SetInterrupt(iKEY1_PRESSED);
    for (int i = 0; i < 300; i++){
        Step();
        if (Keyed() && (GetCWVFOFrequency() != 0) &&
            (si5351.clk_freq[CWClock()] == (uint64_t)GetCWVFOFrequency() * 100)) cw = true;
    }
    std::cout << "  CW keyed " << when << ": CW VFO " << GetCWVFOFrequency() << " Hz, chip "
              << si5351.clk_freq[CWClock()] / 100 << " Hz" << std::endl;
    SetInterrupt(iKEY1_RELEASED); Settle(400);
    Press(TOGGLE_MODE); Settle(50);   // back to SSB
    if (check){
        if (dual) CHECK(ssb, std::string("SSB TX oscillator written to the chip ") + when);
        CHECK(cw, std::string("CW oscillator written to the chip ") + when);
    }
}

/** The correction factor's change for one detent of the Filter encoder */
static int32_t FactorStep(void){
    int32_t f0 = ED.freqCorrectionFactor;
    Turn(iFILTER_INCREASE); Settle(5);
    int32_t step = ED.freqCorrectionFactor - f0;
    Turn(iFILTER_DECREASE); Settle(5);
    return step;
}

/** Turn the Filter encoder to the target, choosing the step with button 15 */
static void DialFactor(int32_t target){
    for (int32_t want : {1000, 100, 10}){
        for (int i = 0; (i < 6) && (FactorStep() != want); i++) Press(15);
        while (ED.freqCorrectionFactor + want <= target) Turn(iFILTER_INCREASE);
    }
}

static void FrequencyCalibration(void){
    std::cout << "\n== Frequency calibration (crystal error " << simCrystalError_ppb << " ppb) ==" << std::endl;
    // Program the TX and CW oscillators on 40 m before the calibration
    Tune("00007074000");
    KeyAndCheckVFOs("before the calibration", false);
    // A 10 MHz standard, in SAM
    Tune("00010000000");
    for (int i = 0; (i < 8) && (ED.modulation[ED.activeVFO] != SAM); i++){ Press(DEMODULATION); Settle(20); }
    if (ED.modulation[ED.activeVFO] != SAM) Abort("could not select SAM");
    OpenCal(CAL_FREQUENCY, UISm_StateId_CALIBRATE_FREQUENCY);
    Settle(1500);
    float e0 = GetSAMCarrierOffset();
    float expect = -(float)simCrystalError_ppb * 1e-9f * 10e6f;
    std::cout << "  factor " << ED.freqCorrectionFactor << ": error " << e0 << " Hz (the carrier is "
              << -expect << " Hz below the dial)" << std::endl;
    CHECK(fabsf(e0 - expect) < 1.0f, "the error reads the carrier's offset in Hz");
    Shot("freq-uncorrected");
    DialFactor(simCrystalError_ppb);
    Settle(2000);
    float e1 = GetSAMCarrierOffset();
    std::cout << "  factor " << ED.freqCorrectionFactor << ": error " << e1 << " Hz" << std::endl;
    CHECK(ED.freqCorrectionFactor == simCrystalError_ppb, "the encoder reaches the crystal error");
    CHECK(fabsf(e1) < 1.0f, "the error is within +/-1 Hz");
    Shot("freq-corrected");
    GoHome();
    CHECK(InReceive(), "HOME leaves frequency calibration for receive");
    for (int i = 0; (i < 8) && ((ED.modulation[ED.activeVFO] == SAM) || (ED.modulation[ED.activeVFO] == AM)); i++){
        Press(DEMODULATION); Settle(20);
    }
    // The correction resets the Si5351; back on the same 40 m frequency, the
    // TX and CW oscillators must be written again
    Tune("00007074000");
    KeyAndCheckVFOs("after the calibration", true);
}

///////////////////////////////////////////////////////////////////////////////
// 2. Receive IQ
///////////////////////////////////////////////////////////////////////////////

struct IQSet { std::vector<float32_t> amp, phs; };
static IQSet RXSettings(void){
    IQSet s;
    for (int b = 0; b < NUMBER_OF_BANDS; b++){ s.amp.push_back(ED.IQAmpCorrectionFactor[b]); s.phs.push_back(ED.IQPhaseCorrectionFactor[b]); }
    return s;
}
static IQSet TXSettings(void){
    IQSet s;
    for (int b = 0; b < NUMBER_OF_BANDS; b++){ s.amp.push_back(ED.IQXAmpCorrectionFactor[b]); s.phs.push_back(ED.IQXPhaseCorrectionFactor[b]); }
    return s;
}
static bool Same(const IQSet &a, const IQSet &b, int band){
    return (a.amp[band] == b.amp[band]) && (a.phs[band] == b.phs[band]);
}

static void ReceiveIQCalibration(void){
    std::cout << "\n== Receive IQ ==" << std::endl;
    OpenCal(CAL_RXIQ, UISm_StateId_CALIBRATE_RX_IQ);
    SelectBandOnScreen(BAND_20M); Settle(300);
    Press(BAND_UP); Settle(300);
    std::cout << "  Band Up from 20 m: band " << bands[ED.currentBand[ED.activeVFO]].name << std::endl;
    Shot("rxiq-band-up");   // the Current Band pane must show the new band

    // HOME during the automatic run
    IQSet before = RXSettings();
    Press(16);
    CHECK(RXIQCalibrationAutoActive(), "button 16 starts the automatic run");
    RunUntil([](){ return ED.currentBand[ED.activeVFO] == FIRST_BAND + 1; }, "the run to reach 80 m");
    Settle(1500);
    int32_t inProgress = ED.currentBand[ED.activeVFO];
    std::cout << "  HOME while the run is on " << bands[inProgress].name << std::endl;
    GoHome();
    CHECK(!RXIQCalibrationAutoActive(), "HOME stops the run");
    CHECK(InReceive(), "HOME returns to receive");
    CHECK(Same(before, RXSettings(), inProgress), "the band in progress keeps its old setting");
    Settle(3000);
    CHECK(Same(before, RXSettings(), inProgress) && !RXIQCalibrationAutoActive(), "nothing changes afterwards");

    // A run in which no setting should give a valid reading
    for (int b = 0; b < NUMBER_OF_BANDS; b++){ ED.IQAmpCorrectionFactor[b] = 1.03f; ED.IQPhaseCorrectionFactor[b] = 0.0f; }
    forceMirroredRXIQ = true;
    OpenCal(CAL_RXIQ, UISm_StateId_CALIBRATE_RX_IQ);
    Settle(300);
    Press(16);
    float best = -1e9f, bestAmp = 0;
    RunUntil([&](){
        int32_t b = ED.currentBand[ED.activeVFO];
        if ((b == FIRST_BAND) && (GetRXDeltaVals(b) > best)){ best = GetRXDeltaVals(b); bestAmp = ED.IQAmpCorrectionFactor[b]; }
        return !RXIQCalibrationAutoActive();
    }, "the run with the mirrored tone to finish");
    forceMirroredRXIQ = false;
    std::cout << "  mirrored tone: best reading on 160 m " << best << " dB, at amplitude " << bestAmp << std::endl;
    int kept = 0;
    for (int b = FIRST_BAND; b <= LAST_BAND; b++){
        if (ED.IQAmpCorrectionFactor[b] == 1.03f) kept++;
        else std::cout << "  " << bands[b].name << " amplitude changed to " << ED.IQAmpCorrectionFactor[b] << std::endl;
    }
    CHECK(kept == LAST_BAND - FIRST_BAND + 1, "with no valid reading every band keeps its amplitude");
    GoHome();
    for (int b = 0; b < NUMBER_OF_BANDS; b++){ ED.IQAmpCorrectionFactor[b] = 1.0f; ED.IQPhaseCorrectionFactor[b] = 0.0f; }

    // The full run
    OpenCal(CAL_RXIQ, UISm_StateId_CALIBRATE_RX_IQ);
    Settle(300);
    Press(16);
    RunUntil([](){ return !RXIQCalibrationAutoActive(); }, "the run to finish");
    int good = 0, total = 0;
    for (int b = FIRST_BAND; b <= LAST_BAND; b++){
        total++;
        float d = GetRXDeltaVals(b);
        if ((d > 50) && (fabsf(ED.IQAmpCorrectionFactor[b] - 1.11f) < 0.03f)) good++;
        std::cout << "  " << bands[b].name << ": amp " << ED.IQAmpCorrectionFactor[b] << " phase "
                  << ED.IQPhaseCorrectionFactor[b] << " delta " << d << " dB" << std::endl;
    }
    CHECK(good == total, "every band converges (amplitude about 1.11, delta above 50 dB)");
    SelectBandOnScreen(BAND_20M); Settle(500);
    Shot("rxiq-done");
    GoHome();
    CHECK(InReceive(), "HOME saves and returns to receive");
}

///////////////////////////////////////////////////////////////////////////////
// 3. Transmit IQ
///////////////////////////////////////////////////////////////////////////////

static void EnterTXIQ(void){ OpenCal(CAL_TXIQ, UISm_StateId_CALIBRATE_TX_IQ); Settle(300); }

static void HomeWhileKeyed(void){
    EnterTXIQ();
    KeyPTT(true);
    CHECK(modeSM.state_id == ModeSm_StateId_CALIBRATE_TX_IQ_MARK, "PTT keys the calibration");
    Settle(300);
    GoHome();
    CHECK(InReceive() && (modeSM.state_id == ModeSm_StateId_SSB_RECEIVE), "HOME while keyed returns to receive, unkeyed");
    KeyPTT(false);
    CHECK(InReceive(), "releasing PTT afterwards leaves it in receive");
}

static void TransmitIQDual(void){
    std::cout << "\n== Transmit IQ (dual VFO) ==" << std::endl;
    HomeWhileKeyed();

    // The run starts from 160 m whatever the band; HOME part way
    EnterTXIQ();
    SelectBandOnScreen(BAND_40M); Settle(200);
    IQSet before = TXSettings();
    Press(16);
    Settle(100);
    std::cout << "  started on 40 m; the run is on " << bands[ED.currentBand[ED.activeVFO]].name << std::endl;
    CHECK(TXIQCalibrationAutoActive() && (ED.currentBand[ED.activeVFO] == FIRST_BAND), "the run starts from 160 m");
    RunUntil([](){ return ED.currentBand[ED.activeVFO] == FIRST_BAND + 1; }, "the run to reach 80 m");
    Settle(1500);
    int32_t inProgress = ED.currentBand[ED.activeVFO];
    CHECK(Keyed(), "the radio is keyed during the run");
    GoHome();
    CHECK(!TXIQCalibrationAutoActive(), "HOME stops the run");
    CHECK(InReceive(), "HOME unkeys and returns to receive");
    CHECK(Same(before, TXSettings(), inProgress), "the band in progress keeps its old setting");
    Settle(3000);
    CHECK(InReceive() && !TXIQCalibrationAutoActive(), "nothing keys afterwards");

    // The full run
    IQSet start = TXSettings();
    EnterTXIQ();
    Press(16);
    bool visited[NUMBER_OF_BANDS] = {false};
    RunUntil([&](){ visited[ED.currentBand[ED.activeVFO]] = true; return !TXIQCalibrationAutoActive(); },
             "the run to finish");
    Settle(100);   // the run's PTT release
    CHECK(!Keyed(), "unkeyed after the run");
    int good = 0, total = 0;
    bool skipped = true;
    for (int b = FIRST_BAND; b <= LAST_BAND; b++){
        if (!IsTXBand(b)){
            if (visited[b] || !Same(start, TXSettings(), b)) skipped = false;
            continue;
        }
        total++;
        float d = GetTXDeltaVals(b);
        if ((d > 60) && (fabsf(ED.IQXAmpCorrectionFactor[b] - 0.906f) < 0.03f)) good++;
        std::cout << "  " << bands[b].name << ": amp " << ED.IQXAmpCorrectionFactor[b] << " phase "
                  << ED.IQXPhaseCorrectionFactor[b] << " IRR " << d << " dB" << std::endl;
    }
    CHECK(good == total, "every transmit band converges (amplitude about 0.906, IRR above 60 dB)");
    CHECK(skipped, "the bands without transmit (general coverage) are skipped and left alone");
    SelectBandOnScreen(BAND_20M); Settle(500);
    Shot("txiq-done");
    GoHome();
    CHECK(InReceive(), "HOME saves and returns to receive");
}

static void TransmitIQSingle(void){
    std::cout << "\n== Transmit IQ (single VFO) ==" << std::endl;
    EnterTXIQ();
    SelectBandOnScreen(BAND_40M); Settle(200);
    CHECK(GetTXIQAttenuation() == 31.5f, "the screen starts at 31.5 dB");
    KeyPTT(true);
    CHECK(Keyed() && (modeSM.state_id == ModeSm_StateId_CALIBRATE_TX_IQ_MARK), "PTT keys");
    std::cout << "  keyed: attenuator " << GetTXAttenuation() << " dB" << std::endl;
    CHECK(fabsf(GetTXAttenuation() - 31.5f) < 0.01f, "the first key-down is at 31.5 dB, not full drive");
    Shot("txiq-single-keyed");
    KeyPTT(false);
    for (int i = 0; i < 12; i++) Turn(iFINETUNE_DECREASE);
    Settle(50);
    float32_t shown = GetTXIQAttenuation();
    KeyPTT(true);
    std::cout << "  screen " << shown << " dB, keyed attenuator " << GetTXAttenuation() << " dB" << std::endl;
    CHECK((shown < 31.5f) && (fabsf(GetTXAttenuation() - shown) < 0.01f), "every key-down applies the screen's attenuation");
    Settle(200);
    GoHome();
    CHECK(InReceive(), "HOME while keyed returns to receive, unkeyed");
    KeyPTT(false);
}

///////////////////////////////////////////////////////////////////////////////
// 4. Power and SWR
///////////////////////////////////////////////////////////////////////////////

static bool InPowerState(PowerCalSm_StateId s){ return powerSM.state_id == s; }

/** Key SSB on 20 m into the load; compare the radio's readings with the bench */
static void CompareWithWattmeter(const char *when, float32_t *err_dB, float32_t *swr){
    Tune("00014200000");
    simConnector = CONNECT_LOAD;
    KeyPTT(true);
    Settle(1500);
    float32_t trueW = SimWattmeter_W(), readW = ReadForwardPower();
    *err_dB = 10.0f * log10f(readW / trueW);
    *swr = ReadSWR();
    std::cout << "  SSB on 20 m " << when << ": wattmeter " << trueW << " W, radio " << readW
              << " W (" << *err_dB << " dB), SWR " << *swr << std::endl;
    KeyPTT(false);
    Settle(200);
}

static void PowerCalibration(void){
    std::cout << "\n== Power and SWR, automatic level 1 ==" << std::endl;
    float32_t err0, swr0;
    CompareWithWattmeter("before", &err0, &swr0);

    OpenCal(CAL_POWER, UISm_StateId_CALIBRATE_POWER);
    Press(1);
    if (!InPowerState(PowerCalSm_StateId_SETUP)) Abort("button 1 did not open the automatic setup");
    Settle();
    while (autoCal.level > 1) Turn(iVOLUME_DECREASE);
    while (ED.SWR_CouplerTurns < RADIO2_TURNS) Turn(iFILTER_INCREASE);
    while (ED.SWR_CouplerTurns > RADIO2_TURNS) Turn(iFILTER_DECREASE);
    Press(MENU_OPTION_SELECT);
    if (!InPowerState(PowerCalSm_StateId_PROMPT)) Abort("no prompt for the load");
    simConnector = CONNECT_LOAD;
    Press(MENU_OPTION_SELECT);
    RunUntil([](){ return InPowerState(PowerCalSm_StateId_PROMPT); }, "the prompt for the open");
    simConnector = CONNECT_OPEN;
    Press(MENU_OPTION_SELECT);
    RunUntil([](){ return InPowerState(PowerCalSm_StateId_PROMPT); }, "the prompt for the short");
    simConnector = CONNECT_SHORT;
    Press(MENU_OPTION_SELECT);
    RunUntil([](){ return InPowerState(PowerCalSm_StateId_REVIEW); }, "the review");
    simConnector = CONNECT_LOAD;
    Settle(100);
    Shot("power-review");
    std::cout << "  failed bands mask 0x" << std::hex << autoCal.failedMask << std::dec << std::endl;
    CHECK(autoCal.failedMask == 0, "every band passes");
    Press(MENU_OPTION_SELECT);
    RunUntil([](){ return InPowerState(PowerCalSm_StateId_DONE); }, "done");
    GoHome();
    CHECK(InReceive(), "HOME returns to receive");

    float32_t err1, swr1;
    CompareWithWattmeter("after", &err1, &swr1);
    CHECK(fabsf(err1) < 1.5f, "the power reading is within 1.5 dB of the wattmeter (level 1)");
    CHECK((swr1 > 1.05f) && (swr1 < 1.25f), "the SWR reads the load's 1.13");
}

///////////////////////////////////////////////////////////////////////////////
// 5. S-meter
///////////////////////////////////////////////////////////////////////////////

static void SMeterLevel(void){
    std::cout << "\n== S-meter level ==" << std::endl;
    Tune("00014200000");
    int32_t b = ED.currentBand[ED.activeVFO];
    std::vector<float32_t> before(ED.dbm_calibration, ED.dbm_calibration + NUMBER_OF_BANDS);
    OpenCalMenu(CAL_SMETER);
    Press(MENU_OPTION_SELECT); Settle(100);
    for (int i = 0; i < 4; i++) Turn(iFILTER_INCREASE);
    Settle(50);
    Shot("smeter");
    Press(MENU_OPTION_SELECT); Settle(50);
    GoHome();
    std::cout << "  " << bands[b].name << ": " << before[b] << " -> " << ED.dbm_calibration[b] << " dB" << std::endl;
    CHECK(fabsf(ED.dbm_calibration[b] - before[b] - 2.0f) < 0.01f, "four detents add 2 dB on this band");
    bool others = true;
    for (int k = 0; k < NUMBER_OF_BANDS; k++)
        if ((k != b) && (ED.dbm_calibration[k] != before[k])) others = false;
    CHECK(others, "the other bands are unchanged");
    CHECK(InReceive(), "HOME returns to receive");
}

///////////////////////////////////////////////////////////////////////////////
// Carrier null (DIRECT_COUPLED_TX builds)
///////////////////////////////////////////////////////////////////////////////

#ifdef DIRECT_COUPLED_TX
extern TransmitCarrierCalSm txcarrSM;
static bool CarrierRunning(void){ return txcarrSM.state_id != TransmitCarrierCalSm_StateId_STANDBY; }

static void CarrierNull(void){
    std::cout << "\n== Carrier null, button 14 ==" << std::endl;
    EnterTXIQ();
    SelectBandOnScreen(BAND_40M); Settle(200);
    if (!HasDualVFOs()){
        Press(14); Settle(300);
        CHECK(!CarrierRunning() && !Keyed(), "button 14 does nothing on a single-VFO radio");
        GoHome();
        return;
    }
    // HOME part way
    std::vector<int16_t> I0(ED.DCOffsetI, ED.DCOffsetI + NUMBER_OF_BANDS), Q0(ED.DCOffsetQ, ED.DCOffsetQ + NUMBER_OF_BANDS);
    Press(14);
    Settle(100);
    CHECK(CarrierRunning() && Keyed(), "button 14 keys and starts the carrier null");
    RunUntil([](){ return ED.currentBand[ED.activeVFO] == FIRST_BAND + 1; }, "the carrier run to reach 80 m");
    Settle(1500);
    int32_t b = ED.currentBand[ED.activeVFO];
    GoHome();
    CHECK(!CarrierRunning() && InReceive(), "HOME stops the carrier run and unkeys");
    CHECK((ED.DCOffsetI[b] == I0[b]) && (ED.DCOffsetQ[b] == Q0[b]), "the band in progress keeps its old offsets");

    // The full run, with its performance log
    std::string logPath = outDir + "/carrier-" + runName + ".csv";
    Serial.createFile(logPath.c_str());
    EnterTXIQ();
    Press(14);
    bool visited[NUMBER_OF_BANDS] = {false};
    RunUntil([&](){ visited[ED.currentBand[ED.activeVFO]] = true; return !CarrierRunning(); },
             "the carrier run to finish");
    Settle(100);   // the run's PTT release
    Serial.closeFile();
    std::cout << "  performance log: " << logPath << std::endl;
    CHECK(!Keyed(), "unkeyed after the carrier run");
    int nulled = 0, total = 0;
    bool skipped = true;
    for (int k = FIRST_BAND; k <= LAST_BAND; k++){
        if (!IsTXBand(k)){ if (visited[k]) skipped = false; continue; }
        total++;
        if (GetTXCarrierVals(k) > 30) nulled++;
        std::cout << "  " << bands[k].name << ": I " << ED.DCOffsetI[k] << " Q " << ED.DCOffsetQ[k]
                  << " carrier " << GetTXCarrierVals(k) << " dB below the tone" << std::endl;
    }
    CHECK(skipped, "the bands without transmit are skipped");
    CHECK(nulled == total, "the carrier is nulled to more than 30 dB below the tone on every transmit band");
    SelectBandOnScreen(BAND_20M); Settle(500);
    Shot("carrier-done");
    GoHome();
}
#endif

int main(int argc, char *argv[]){
    if ((argc < 3) || ((std::string(argv[1]) != "dual") && (std::string(argv[1]) != "single"))){
        std::cerr << "Usage: " << argv[0] << " <dual|single> <output directory>" << std::endl;
        return 2;
    }
    runName = argv[1];
    outDir = argv[2];
    std::filesystem::create_directories(outDir);
    bool single = (runName == "single");

    // The bench
    simCrystalError_ppb = 2500;
    Si5351_Mock_SetDeviceAddress(single ? SI5351_BUS_BASE_ADDR : SI5351_DUAL_VFO_ADDR);
    Adafruit_I2CDevice::setMockADCProvider(SimBridgeADC);
    Q_out_L_Ex.setAudioChannel(2);   // the transmit IQ outputs, for the loopback
    Q_out_R_Ex.setAudioChannel(3);

    SimStartRadio(outDir + "/storage-" + runName);
    setAudioInputSource(AUDIO_SOURCE_SINGLE_TONE);
    simStepHook = Bench;
    start_timer1ms();
    RunUntil([](){ return uiSM.state_id == UISm_StateId_HOME; }, "home screen", 20000);
    Settle(300);
    std::cout << "Hardware: " << (HasDualVFOs() ? "dual" : "single") << " VFO" << std::endl;
    CHECK(HasDualVFOs() == !single, "the firmware detects the VFO type at start-up");

    const char *only = getenv("WALK_ONLY");
    auto run = [only](const char *s){
        return !only || (("," + std::string(only) + ",").find("," + std::string(s) + ",") != std::string::npos);
    };
#ifdef DIRECT_COUPLED_TX
    if (run("carrier")) CarrierNull();
#else
    if (run("freq")) FrequencyCalibration();
    if (run("rxiq")) ReceiveIQCalibration();
    if (run("txiq")) { if (single) TransmitIQSingle(); else TransmitIQDual(); }
    if (run("power")) PowerCalibration();
    if (run("smeter")) SMeterLevel();
#endif

    stop_timer1ms();
    Adafruit_I2CDevice::setMockADCProvider(nullptr);
    std::cout << "\n" << (failures ? "FAILED: " : "PASSED: ") << (checks - failures) << " of " << checks
              << " checks passed" << std::endl;
    RA8875_SDL_Cleanup();
    std::filesystem::remove_all(outDir + "/storage-" + runName);
    return failures;
}
