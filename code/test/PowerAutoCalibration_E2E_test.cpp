/**
 * End-to-end tests of the automatic power and SWR calibration.
 *
 * These run the radio as the firmware does: the real 1 ms timer function
 * (tick1ms() from Globals.cpp) on its own thread, locked to the wall clock, and
 * loop() on the test's thread. The operator works only through the front panel:
 * the menu to reach power calibration, buttons and encoders through the
 * interrupt queue, and the standards on the antenna connector.
 *
 * A simulated bench answers the AD7991 bridge conversions from the hardware
 * state alone: which band's LPF is selected, whether the radio transmits CW or
 * SSB, the TX attenuator or the SSB DSP gain, and what the operator has fitted.
 * Each test runs twice, once as each of the two radios measured after their L44
 * rebuilds (the bench sheets in code/tools/swr): their bridge lines and leaks,
 * PA curves, SSB tone transfer, the different power the PA puts into an open or
 * a short, radio 1's 10 m detector error in saturation and radio 2's 10 m
 * oscillation into the short.
 * The firmware's own bridge path (PerformSignalProcessing() ->
 * PerformSWRBridgeReading() -> AD7991 -> AccumulateSWRBridgeStats()) carries
 * the readings, so the calibration sees nothing the radio would not.
 *
 * After every loop() a monitor checks the hardware against the calibration's
 * state and records each key-down, so the tests can check:
 * - control flow: the standards in order, every band, the review and storage;
 * - timing: settle, collect, rest and band-settle times, and the PA's
 *   key-down time into the open and short;
 * - hardware: never transmitting outside a point, true receive while resting
 *   on the open and short and while the operator changes the connector, the
 *   attenuator and DSP gain as commanded, the right LPF, and no band change
 *   while transmitting.
 */

#include "gtest/gtest.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <string>
#include <cmath>

#include "../src/PhoenixSketch/SDT.h"
#include "Adafruit_I2CDevice.h"

#define GETHWRBITS(LSB,len) ((hardwareRegister >> LSB) & ((1 << len) - 1))

void tick1ms(void);           // Globals.cpp: the firmware's 1 ms timer interrupt
extern float32_t TXgainDSP;   // DSP.cpp: the gain of the last transmitted block
bool AutoAnyFailed(void);     // HardwareSm_PowerAutoCalibration.cpp

// Shortened timing, so a session over all 11 bands takes about half a minute
static const uint32_t SETTLE_MS = 20;
static const uint32_t COLLECT_MS = 50;
static const uint32_t REST_MS = 25;
static const uint32_t BAND_SETTLE_MS = 15;
static const uint32_t METER_TIMEOUT_MS = 5000;
static const uint32_t MIN_READINGS = 2;     // the bridge is read about every 11 ms
static const uint32_t KEYED_SLACK_MS = 60;  // service latency over a point's key-down
static const uint32_t LEVEL1_CW_POINTS = 12; // the load's attenuator sweep
static const uint32_t LEVEL3_CW_POINTS = 8;  // the load's wattmeter points

///////////////////////////////////////////////////////////////////////////////
// The 1 ms timer
///////////////////////////////////////////////////////////////////////////////

static std::atomic<bool> timer_running{false};
static std::thread timer_thread;

/**
 * Run tick1ms() once per millisecond of wall-clock time, as in Radio_test.cpp:
 * the state machines count time in DO events, while the test measures it with
 * millis(), so the two must not drift apart.
 */
static void start_timer1ms() {
    if (timer_running.load()) return;
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
    if (!timer_running.load()) return;
    timer_running.store(false);
    if (timer_thread.joinable())
        timer_thread.join();
}

///////////////////////////////////////////////////////////////////////////////
// The bench: two radios as measured, and the standards on the connector
///////////////////////////////////////////////////////////////////////////////

enum Connector { CONNECT_LOAD, CONNECT_OPEN, CONNECT_SHORT };
static const char *ConnectorName(Connector c){
    return c == CONNECT_LOAD ? "load" : (c == CONNECT_OPEN ? "open" : "short");
}

/**
 * A radio as measured after its L44 rebuild, on the fixed firmware: the bench
 * sheets code/tools/swr/swr_bench_measurements_radio1_adcfix.csv and
 * _radio2_l44_shielded.csv. Each value is at 160, 40, 20 and 10 m; the other
 * bands are interpolated in log frequency and 6 m takes 10 m's.
 */
struct RadioProfile {
    const char *name;
    uint8_t turns;                     // L44 turns per core
    float32_t m_f[4], o_f[4];          // forward line, wattmeter fit with the ceiling rule
    float32_t m_r[4], o_r[4];          // reflected line, joint open/short fit
    float32_t gap_dB[4];               // open minus short intercept: the bridge's leak
    float32_t Psat_mW[4], k[4];        // PA curve, fitted to the wattmeter
    float32_t transfer[4];             // SSB tone: dB out per dB of drive
    float32_t open_dB[4], short_dB[4]; // forward power into the open and short against 50 Ohm, same drive
    float32_t S50[4];                  // the wattmeter chain's SWR, as the calibration reads it
    int32_t oscillatesIntoShort;       // band whose PA oscillates into the short, or -1
    float32_t ceilingError_dB;         // 10 m forward detector at 0 dB attenuation; + reads high
};

static const RadioProfile RADIO1 = {
    "Radio1", 10,
    {24.80f, 24.68f, 24.41f, 24.27f}, {-5.12f, -5.11f, -6.82f, -6.64f},
    {25.08f, 25.25f, 24.87f, 25.08f}, {-3.12f, -2.33f, -4.21f, -2.90f},
    {0.02f, -0.03f, 0.04f, 0.55f},
    {11295, 15150, 10073, 6591}, {30.5f, 15.9f, 3.6f, 16.8f},
    {0.922f, 0.950f, 0.947f, 0.927f},
    {-2.86f, 2.71f, 1.05f, -1.26f}, {2.29f, -2.91f, -2.50f, 1.45f},
    {1.080f, 1.078f, 1.128f, 1.357f},
    -1, +2.28f,
};

static const RadioProfile RADIO2 = {
    "Radio2", 20,
    {24.79f, 24.62f, 24.99f, 25.13f}, {1.93f, 1.89f, 1.75f, 1.93f},
    {24.82f, 24.54f, 24.96f, 25.13f}, {1.90f, 1.66f, 1.68f, 1.56f},
    {-0.12f, -0.18f, -0.38f, 0.0f},
    {13522, 17530, 10118, 5015}, {35.6f, 20.4f, 22.7f, 25.3f},
    {0.956f, 0.952f, 0.948f, 0.907f},
    {-3.14f, -2.54f, 4.93f, -0.64f}, {3.72f, 0.25f, -3.80f, 0.0f},
    {1.085f, 1.083f, 1.126f, 1.198f},
    BAND_10M, -0.55f,
};

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

/**
 * Radio 1's 10 m forward detector over-reads all through the PA's saturation
 * (ten_m_ceiling/ceil10m.json), scaled to the profile's error at 0 dB. The
 * reflected detector rises with it, by about 0.8 of the forward (wiki).
 */
static float32_t CeilingError_dB(const RadioProfile *r, int32_t band, float32_t att){
    if (band != BAND_10M) return 0.0f;
    static const float32_t a[5] = {0, 1, 3, 6, 12};
    static const float32_t e[5] = {1.0f, 0.842f, 0.675f, 0.412f, 0.0f};
    if (att >= a[4]) return 0.0f;
    int i = 0;
    while (att > a[i + 1]) i++;
    return r->ceilingError_dB * (e[i] + (e[i + 1] - e[i]) * (att - a[i]) / (a[i + 1] - a[i]));
}

struct Bench {
    const RadioProfile *radio = &RADIO1;
    Connector connector = CONNECT_LOAD;
    int32_t extraOscillationBand = -1;         // a test may add an oscillation...
    Connector extraOscillationInto = CONNECT_OPEN; // ...into this standard...
    bool extraOscillationBoth = false;             // ...or into the open and the short
    uint32_t conversions = 0;                  // for the readings' dither

    float32_t m_f(int32_t b) const { return AtBand(radio->m_f, b); }
    float32_t b_f(int32_t b) const { return AtBand(radio->o_f, b) - SWRCAL_OFFSET_CONSTANT_DB; }
    float32_t m_r(int32_t b) const { return AtBand(radio->m_r, b); }
    float32_t b_r(int32_t b) const { return AtBand(radio->o_r, b) - SWRCAL_OFFSET_CONSTANT_DB; }
    float32_t gap(int32_t b) const { return AtBand(radio->gap_dB, b); }
    float32_t Psat_mW(int32_t b) const { return AtBand(radio->Psat_mW, b); }
    float32_t k(int32_t b) const { return AtBand(radio->k, b); }
    float32_t S50(int32_t b) const { return AtBand(radio->S50, b); }
    float32_t ssbGainFor3W(int32_t b) const { return 0.5f + 0.1f * b; }
    bool Oscillates(int32_t b, Connector c) const {
        return ((b == radio->oscillatesIntoShort) && (c == CONNECT_SHORT)) ||
               ((b == extraOscillationBand) &&
                ((c == extraOscillationInto) || (extraOscillationBoth && (c != CONNECT_LOAD))));
    }
};
static Bench bench;

/** Band whose LPF the hardware has selected, or -1 */
static int32_t HardwareBand(void){
    for (int32_t b = BAND_160M; b <= BAND_6M; b++)
        if (BandToBCD(b) == GETHWRBITS(LPFBAND0BIT, 4))
            return b;
    return -1;
}

/** The radio is putting out RF: T/R in TX and the CW carrier on, or SSB modulation */
static bool Transmitting(void){
    return GET_BIT(hardwareRegister, RXTXBIT) &&
           (GET_BIT(hardwareRegister, CWBIT) || GET_BIT(hardwareRegister, MODEBIT));
}

/** True forward power on the line from the hardware state and the standard, in dBm */
static float32_t ForwardPower_dBm(void){
    int32_t b = HardwareBand();
    if (b < 0) return -30.0f;
    if (GET_BIT(hardwareRegister, CWBIT)){
        float32_t att = GETHWRBITS(TXATTLSB, 6) / 2.0f;
        return 10.0f * log10f(bench.Psat_mW(b) * tanhf(bench.k(b) * powf(10.0f, -att / 10.0f)));
    }
    float32_t gain_dB;
    if (!GetSSBGainOverride(&gain_dB)){
        bool PAsel;
        gain_dB = ED.PowerCal_20W_DSP_Gain_correction_dB[b]
                  + CalculateSSBTXGain(ED.powerOutSSB[b], &PAsel);
    }
    float32_t P = 10.0f * log10f(3000.0f)
                  + AtBand(bench.radio->transfer, b) * (gain_dB - bench.ssbGainFor3W(b));
    // The PA puts a different power into an open or a short
    if (bench.connector == CONNECT_OPEN) P += AtBand(bench.radio->open_dB, b);
    if (bench.connector == CONNECT_SHORT) P += AtBand(bench.radio->short_dB, b);
    return P;
}

/** The AD7991's answer for a channel: 0 forward, 1 reflected; counts = mV */
static uint16_t BridgeADC(uint8_t channel){
    if (!Transmitting())
        return channel == 0 ? 230 : 355;   // the detectors' key-up floor
    int32_t b = HardwareBand();
    if (b < 0) return 0;
    // A bounded dither, like the readings' scatter but never enough to tip a check
    float32_t dither = 2.0f * sinf(0.7f * (float32_t)(bench.conversions++));
    if (bench.Oscillates(b, bench.connector))
        return (uint16_t)lroundf(1950.0f + dither); // stuck whatever the drive
    float32_t Pf = ForwardPower_dBm();
    float32_t over = GET_BIT(hardwareRegister, CWBIT)
        ? CeilingError_dB(bench.radio, b, GETHWRBITS(TXATTLSB, 6) / 2.0f) : 0.0f;
    // What the reflected detector reads as power: the load's reflection, or all of
    // the forward power with the leak adding on one standard and taking on the other
    float32_t Pr = Pf;
    switch (bench.connector){
        case CONNECT_LOAD:{
            float32_t S = bench.S50(b);
            Pr += 20.0f * log10f((S - 1.0f) / (S + 1.0f)) + 0.8f * over;
            break;
        }
        case CONNECT_OPEN:  Pr -= bench.gap(b) / 2.0f; break;
        case CONNECT_SHORT: Pr += bench.gap(b) / 2.0f; break;
    }
    float32_t V = (channel == 0) ? bench.m_f(b) * (Pf + over - bench.b_f(b))
                                 : bench.m_r(b) * (Pr - bench.b_r(b));
    V += dither;
    if (V < 0) V = 0;
    if (V > 4095) V = 4095;
    return (uint16_t)lroundf(V);
}

///////////////////////////////////////////////////////////////////////////////
// The monitor: hardware checks after every loop(), key-downs and state times
///////////////////////////////////////////////////////////////////////////////

/**
 * A key-down, bounded by the monitor's looks. It began after lastOff_ms (the
 * last look unkeyed) and by firstOn_ms (the first look keyed); it ended after
 * lastOn_ms and by firstOff_ms. A loop() can be delayed while the suite runs,
 * so a check fails only when the bound in its favour does.
 */
struct KeyInterval {
    uint32_t lastOff_ms, firstOn_ms, lastOn_ms, firstOff_ms;
    Connector connector;
    int32_t band;
    bool ssb;
    uint32_t shortest(void) const { return lastOn_ms - firstOn_ms; }
    uint32_t longest(void) const { return firstOff_ms - lastOff_ms; }
};

/** A visit to a state, bounded the same way */
struct StateVisit {
    PowerCalSm_StateId state;
    uint32_t longest_ms;   // from the last look before it to the first look after it
};

static bool InState(PowerCalSm_StateId s, std::initializer_list<PowerCalSm_StateId> set){
    for (PowerCalSm_StateId x : set)
        if (s == x) return true;
    return false;
}

// The session fields the hardware checks depend on, read together
struct Snapshot {
    int32_t band;
    AutoCalStage stage;
    bool ssbMode;
    float32_t drive_dB;
};

class Monitor {
public:
    std::vector<KeyInterval> keyDowns;
    std::vector<StateVisit> visits;
    uint32_t samples = 0;
    uint32_t violations = 0;
    std::string firstViolation;
    uint32_t ssbBlocksAtCommandedGain = 0;
    uint32_t ssbBlocksElsewhere = 0;

    void Reset(void){ *this = Monitor(); }

    PowerCalSm_StateId seen = PowerCalSm_StateId_ROOT; // the state the checks saw

    void Violation(const std::string &what){
        if (violations++ == 0)
            firstViolation = what + " (state " + PowerCalSm_state_id_to_string(seen) +
                             ", mode " + ModeSm_state_id_to_string(modeSM.state_id) + ", band " +
                             std::to_string(autoCal.band) + ", t " + std::to_string(millis()) + " ms)";
    }

    void Sample(void){
        // Take a consistent view: skip the checks if the timer moved the state
        // machine while we looked
        PowerCalSm_StateId s1 = powerSM.state_id;
        bool pending1 = autoCal.servicePending;
        uint64_t hw = hardwareRegister;
        ModeSm_StateId mode = modeSM.state_id;
        Snapshot snap = {autoCal.band, autoCal.stage, autoCal.ssbMode, autoCal.drive_dB};
        bool tx = Transmitting();
        // The session fields too: one timer dispatch can update them (a transition
        // action) before it moves state_id
        bool stable = (s1 == powerSM.state_id) && (pending1 == autoCal.servicePending) &&
                      (hw == hardwareRegister) && (mode == modeSM.state_id) &&
                      (snap.band == autoCal.band) && (snap.stage == autoCal.stage) &&
                      (snap.ssbMode == autoCal.ssbMode) && (snap.drive_dB == autoCal.drive_dB);
        // A composite state is state_id only while one dispatch passes through it to
        // a leaf. On the Teensy the timer interrupt finishes before loop() runs again,
        // so loop() never sees one; here the timer is a parallel thread and can.
        if (InState(s1, {PowerCalSm_StateId_ROOT, PowerCalSm_StateId_MANUAL,
                         PowerCalSm_StateId_AUTO, PowerCalSm_StateId_POINT}))
            stable = false;
        uint32_t now = millis();
        samples++;
        seen = s1;

        TrackKeyDowns(tx, now);
        TrackStates(s1, now);
        lastLook_ms = now;
        if (!stable || !autoCal.active)
            return;
        CheckTransmitOnlyInPoints(s1, pending1, tx);
        if (!pending1)
            CheckHardware(s1, mode, tx, snap);
    }

    void Finish(void){
        if (keying)
            Violation("still transmitting at the end");
    }

private:
    bool keying = false;
    KeyInterval current = {};
    float32_t lastTXgainDSP = -1000.0f;
    PowerCalSm_StateId lastState = PowerCalSm_StateId_ROOT;
    uint32_t stateEnteredAfter = 0;  // the last look before lastState was seen
    uint32_t lastLook_ms = 0;        // the previous look

    void TrackKeyDowns(bool tx, uint32_t now){
        if (tx && !keying){
            keying = true;
            current = {lastLook_ms, now, now, now, bench.connector, HardwareBand(),
                       (bool)GET_BIT(hardwareRegister, MODEBIT)};
        } else if (tx && keying){
            current.lastOn_ms = now;
            if (HardwareBand() != current.band)
                Violation("band changed while transmitting");
        } else if (!tx && keying){
            keying = false;
            current.firstOff_ms = now;
            keyDowns.push_back(current);
        }
        // The SSB tone must leave the DSP chain at the commanded gain. Judge only
        // blocks computed since the last look: the first look after a key-down
        // still sees the previous point's block.
        float32_t gain;
        if (tx && GET_BIT(hardwareRegister, MODEBIT) && GetSSBGainOverride(&gain) &&
            (TXgainDSP != lastTXgainDSP)){
            if (fabsf(TXgainDSP - gain) < 1e-4f) ssbBlocksAtCommandedGain++;
            else ssbBlocksElsewhere++;
        }
        lastTXgainDSP = TXgainDSP;
    }

    void TrackStates(PowerCalSm_StateId s, uint32_t now){
        if (s != lastState){
            visits.push_back({lastState, now - stateEnteredAfter});
            lastState = s;
            stateEnteredAfter = lastLook_ms;
        }
    }

    void CheckTransmitOnlyInPoints(PowerCalSm_StateId s, bool pending, bool tx){
        // While the service has work pending, the hardware may still be one step
        // behind the state machine (one loop() at most)
        if (pending || !tx) return;
        if (!InState(s, {PowerCalSm_StateId_KEYDOWN, PowerCalSm_StateId_METER_ENTRY,
                         PowerCalSm_StateId_COLLECT, PowerCalSm_StateId_STORE}))
            Violation("transmitting outside a point");
    }

    void CheckHardware(PowerCalSm_StateId s, ModeSm_StateId mode, bool tx, const Snapshot &a){
        int32_t band = a.band;
        bool inBandWork = !InState(s, {PowerCalSm_StateId_SETUP, PowerCalSm_StateId_PROMPT,
                                       PowerCalSm_StateId_FIT_ALL, PowerCalSm_StateId_REVIEW,
                                       PowerCalSm_StateId_DONE});
        if (inBandWork && (GETHWRBITS(LPFBAND0BIT, 4) != BandToBCD(band)))
            Violation("LPF is not the calibration band's");
        if (GET_BIT(hardwareRegister, PA100WBIT))
            Violation("100 W PA selected");

        switch (mode){
            case ModeSm_StateId_CALIBRATE_POWER_MARK:
                if (!tx || !GET_BIT(hardwareRegister, CWBIT) || GET_BIT(hardwareRegister, MODEBIT) ||
                    !GET_BIT(hardwareRegister, CWVFOBIT) || GET_BIT(hardwareRegister, RXVFOBIT) ||
                    !GET_BIT(hardwareRegister, TXBPFBIT) || GET_BIT(hardwareRegister, RXBPFBIT) ||
                    !GET_BIT(hardwareRegister, XVTRBIT))
                    Violation("CW key-down hardware wrong");
                if (GETHWRBITS(TXATTLSB, 6) != (uint32_t)lroundf(2.0f * a.drive_dB))
                    Violation("TX attenuator is not the point's attenuation");
                if (a.ssbMode)
                    Violation("CW key-down for an SSB point");
                break;
            case ModeSm_StateId_CALIBRATE_OFFSET_MARK:{
                float32_t gain;
                if (!tx || GET_BIT(hardwareRegister, CWBIT) || !GET_BIT(hardwareRegister, MODEBIT) ||
                    GET_BIT(hardwareRegister, CWVFOBIT) || !GET_BIT(hardwareRegister, TXBPFBIT) ||
                    GET_BIT(hardwareRegister, RXBPFBIT) || !GET_BIT(hardwareRegister, XVTRBIT))
                    Violation("SSB key-down hardware wrong");
                if (GETHWRBITS(TXATTLSB, 6) != 0)
                    Violation("TX attenuator not 0 dB for SSB");
                if (GETHWRBITS(RXATTLSB, 6) != 63)
                    Violation("RX attenuator not 31.5 dB while transmitting");
                if (!GetSSBGainOverride(&gain) || (fabsf(gain - a.drive_dB) > 1e-4f))
                    Violation("SSB DSP gain is not the point's");
                if (!a.ssbMode)
                    Violation("SSB key-down for a CW point");
                break;
            }
            case ModeSm_StateId_CALIBRATE_OFFSET_SPACE:
                // True receive: T/R relay in RX, PA out of the path
                if (tx || GET_BIT(hardwareRegister, RXTXBIT) || GET_BIT(hardwareRegister, TXBPFBIT) ||
                    !GET_BIT(hardwareRegister, RXBPFBIT) || GET_BIT(hardwareRegister, CWBIT))
                    Violation("not in receive while unkeyed on the SSB tone");
                break;
            case ModeSm_StateId_CALIBRATE_POWER_SPACE:
                if (tx)
                    Violation("transmitting in CW space");
                if (a.stage != AUTO_STAGE_LOAD)
                    Violation("CW space during the open or short: T/R relay in TX");
                break;
            default:
                Violation("mode outside power calibration");
                break;
        }
        // Resting between open and short points, and changing the connector,
        // must be in true receive
        if ((s == PowerCalSm_StateId_REST) && (mode != ModeSm_StateId_CALIBRATE_OFFSET_SPACE))
            Violation("REST not in true receive");
        if ((s == PowerCalSm_StateId_PROMPT) && (a.stage != AUTO_STAGE_LOAD) &&
            (mode != ModeSm_StateId_CALIBRATE_OFFSET_SPACE))
            Violation("connector change not in true receive");
    }
};
static Monitor monitor;

///////////////////////////////////////////////////////////////////////////////
// The fixture: the radio, the menus and the operator
///////////////////////////////////////////////////////////////////////////////

class PowerAutoCalE2E : public ::testing::TestWithParam<const RadioProfile *> {
protected:
    void SetUp() override {
        hardwareRegister = 0;
        Q_in_L.setChannel(0);
        Q_in_R.setChannel(1);
        Q_in_L.clear();
        Q_in_R.clear();
        Q_in_L_Ex.setChannel(0);
        Q_in_R_Ex.setChannel(1);
        Q_in_L_Ex.clear();
        Q_in_R_Ex.clear();
        // Start the clock once per process. The mock's millis() is signed, so
        // restarting it would leave DSP.cpp's bridge-read limiter (a uint32_t
        // timestamp) waiting until the clock caught up with the previous test.
        static bool clockStarted = false;
        if (!clockStarted){
            StartMillis();
            clockStarted = true;
        }

        // Radio startup, as in setup()
        InitializeStorage();
        InitializeFrontPanel();
        InitializeSignalProcessing();
        InitializeAudio();
        InitializeDisplay();
        InitializeRFHardware();
        modeSM.vars.waitDuration_ms = CW_TRANSMIT_SPACE_TIMEOUT_MS;
        modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
        ModeSm_start(&modeSM);
        uiSM.vars.splashDuration_ms = 1;
        UISm_start(&uiSM);
        UpdateAudioIOState();
        digitalWrite(KEY1, 1);
        digitalWrite(KEY2, 1);

        // An uncalibrated radio: the PA curve, SSB gain and bridge at their factory defaults
        ED.PA100Wactive = false;
        ED.SWR_CouplerTurns = 10;
        const struct config_t factory;
        for (int32_t b = FIRST_BAND; b <= LAST_BAND; b++){
            ED.PowerCal_20W_DSP_Gain_correction_dB[b] = factory.PowerCal_20W_DSP_Gain_correction_dB[b];
            ED.PowerCal_20W_Psat_mW[b] = factory.PowerCal_20W_Psat_mW[b];
            ED.PowerCal_20W_kindex[b] = factory.PowerCal_20W_kindex[b];
            ED.PowerCal_20W_shape[b] = factory.PowerCal_20W_shape[b];
            ED.SWR_F_SlopeAdj[b] = factory.SWR_F_SlopeAdj[b];
            ED.SWR_R_SlopeAdj[b] = factory.SWR_R_SlopeAdj[b];
            ED.SWR_F_Offset[b] = factory.SWR_F_Offset[b];
            ED.SWR_R_Offset[b] = factory.SWR_R_Offset[b];
            ED.SWR_CalLevel[b] = 0;
        }
        extern size_t primaryMenuIndex;
        extern size_t secondaryMenuIndex;
        primaryMenuIndex = 0;
        secondaryMenuIndex = 0;

        bench = Bench();
        bench.radio = GetParam();
        monitor.Reset();
        Adafruit_I2CDevice::setMockADCProvider(BridgeADC);
        start_timer1ms();
    }

    void TearDown() override {
        stop_timer1ms();
        Adafruit_I2CDevice::setMockADCProvider(nullptr);
    }

    /** One pass of the main loop, then the monitor */
    void Step(void){
        loop();
        monitor.Sample();
        MyDelay(1);
    }

    void Press(int32_t button){
        SetButton(button);
        SetInterrupt(iBUTTON_PRESSED);
        Step();
    }

    void Turn(InterruptType encoder){
        SetInterrupt(encoder);
        Step();
    }

    /** Home -> Main menu -> Calibration -> Power, as an operator would */
    void NavigateToPowerCalibration(void){
        extern size_t primaryMenuIndex;
        extern size_t secondaryMenuIndex;
        for (int k = 0; k < 20 && uiSM.state_id != UISm_StateId_HOME; k++){
            loop(); MyDelay(5);
        }
        ASSERT_EQ(uiSM.state_id, UISm_StateId_HOME);
        Press(MAIN_MENU_UP);
        ASSERT_EQ(uiSM.state_id, UISm_StateId_MAIN_MENU);
        for (int i = 0; i < 6; i++)
            Turn(iFILTER_INCREASE);
        ASSERT_EQ(primaryMenuIndex, 6u) << "Calibration";
        Press(MENU_OPTION_SELECT);
        ASSERT_EQ(uiSM.state_id, UISm_StateId_SECONDARY_MENU);
        for (int i = 0; i < 4; i++)
            Turn(iFILTER_INCREASE);
        ASSERT_EQ(secondaryMenuIndex, 4u) << "Power";
        Press(MENU_OPTION_SELECT);
        Step(); // the queued iCALIBRATE_POWER
        ASSERT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
        ASSERT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
        ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1);
    }

    /** Button 1, then the level on the volume encoder; shortens the timing */
    void StartAuto(uint8_t level){
        Press(1);
        ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_SETUP);
        ASSERT_TRUE(PowerCalAutoActive());
        powerSM.vars.bandSettle_ms = BAND_SETTLE_MS;
        powerSM.vars.settle_ms = SETTLE_MS;
        powerSM.vars.rest_ms = REST_MS;
        powerSM.vars.meterTimeout_ms = METER_TIMEOUT_MS;
        autoCal.collectLoad_ms = COLLECT_MS;
        autoCal.collectReflection_ms = COLLECT_MS;
        autoCal.minReadings = MIN_READINGS;
        while (autoCal.level > level) Turn(iVOLUME_DECREASE);
        while (autoCal.level < level) Turn(iVOLUME_INCREASE);
        ASSERT_EQ(autoCal.level, level);
        // The radio's L44 turns on the filter encoder
        while (ED.SWR_CouplerTurns < bench.radio->turns) Turn(iFILTER_INCREASE);
        while (ED.SWR_CouplerTurns > bench.radio->turns) Turn(iFILTER_DECREASE);
        ASSERT_EQ(ED.SWR_CouplerTurns, bench.radio->turns);
    }

    /**
     * Run the main loop until pred() holds. The operator answers every
     * wattmeter entry: a turn of the filter encoder, the meter's reading
     * dialled in, then SELECT.
     */
    template <typename Pred>
    bool RunUntil(Pred pred, uint32_t timeout_ms = 60000){
        uint32_t t0 = millis();
        while (!pred()){
            if (millis() - t0 > timeout_ms)
                return false;
            if ((powerSM.state_id == PowerCalSm_StateId_METER_ENTRY) && !autoCal.servicePending &&
                Transmitting()){
                Turn(iFILTER_INCREASE);
                SetMeasuredPower(powf(10.0f, ForwardPower_dBm() / 10.0f) / 1000.0f);
                meterReadings++;
                Press(MENU_OPTION_SELECT);
            } else {
                Step();
            }
        }
        return true;
    }

    bool RunUntilState(PowerCalSm_StateId s, uint32_t timeout_ms = 60000){
        return RunUntil([s](){ return powerSM.state_id == s; }, timeout_ms);
    }

    /** At a PROMPT: fit the standard on the connector, then SELECT */
    void Fit(Connector c){
        ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_PROMPT);
        bench.connector = c;
        Press(MENU_OPTION_SELECT);
    }

    /** Run the three standards from the first PROMPT to REVIEW */
    void RunStandards(bool skipLoad = false){
        ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_PROMPT);
        ASSERT_EQ(autoCal.stage, AUTO_STAGE_LOAD);
        if (skipLoad){
            Press(12);
        } else {
            Fit(CONNECT_LOAD);
            ASSERT_TRUE(RunUntilState(PowerCalSm_StateId_PROMPT)) << "load stage did not finish";
        }
        ASSERT_EQ(autoCal.stage, AUTO_STAGE_OPEN);
        Fit(CONNECT_OPEN);
        ASSERT_TRUE(RunUntilState(PowerCalSm_StateId_PROMPT)) << "open stage did not finish";
        ASSERT_EQ(autoCal.stage, AUTO_STAGE_SHORT);
        Fit(CONNECT_SHORT);
        ASSERT_TRUE(RunUntilState(PowerCalSm_StateId_REVIEW)) << "short stage did not finish";
    }

    /** SWR the stored calibration reads for a load with |Gamma| at 5 W */
    float32_t StoredSWR(int32_t b, float32_t gamma){
        float32_t Pf = 10.0f * log10f(5000.0f);
        float32_t Vf = bench.m_f(b) * (Pf - bench.b_f(b));
        float32_t Vr = bench.m_r(b) * (Pf + 20.0f * log10f(gamma) - bench.b_r(b));
        return SWRCalSWR(Vf, Vr, 25.0f + ED.SWR_F_SlopeAdj[b], ED.SWR_F_Offset[b] - SWRCAL_OFFSET_CONSTANT_DB,
                         25.0f + ED.SWR_R_SlopeAdj[b], ED.SWR_R_Offset[b] - SWRCAL_OFFSET_CONSTANT_DB);
    }

    /** Worst error of the stored forward line, 0.1 to 15 W, in dB */
    float32_t ForwardReadError_dB(int32_t b){
        float32_t worst = 0;
        for (float32_t P_W : {0.1f, 0.5f, 2.0f, 5.0f, 15.0f}){
            float32_t P = 10.0f * log10f(1000.0f * P_W);
            float32_t V = bench.m_f(b) * (P - bench.b_f(b));
            float32_t read = SWRCalPower_dBm(V, 25.0f + ED.SWR_F_SlopeAdj[b], ED.SWR_F_Offset[b] - SWRCAL_OFFSET_CONSTANT_DB);
            worst = fmaxf(worst, fabsf(read - P));
        }
        return worst;
    }

    /** Worst error of the stored reflected line, 0.1 to 2 W, less the expected bias, in dB */
    float32_t ReflectedReadError_dB(int32_t b, float32_t expectedBias_dB){
        float32_t worst = 0;
        for (float32_t P_W : {0.1f, 0.3f, 1.0f, 2.0f}){
            float32_t P = 10.0f * log10f(1000.0f * P_W);
            float32_t V = bench.m_r(b) * (P - bench.b_r(b));
            float32_t read = SWRCalPower_dBm(V, 25.0f + ED.SWR_R_SlopeAdj[b], ED.SWR_R_Offset[b] - SWRCAL_OFFSET_CONSTANT_DB);
            worst = fmaxf(worst, fabsf(read - P - expectedBias_dB));
        }
        return worst;
    }

    std::vector<KeyInterval> KeyDowns(int32_t band, Connector c){
        std::vector<KeyInterval> v;
        for (const KeyInterval &k : monitor.keyDowns)
            if ((k.band == band) && (k.connector == c))
                v.push_back(k);
        return v;
    }

    /** Timing and key-down checks over the whole session */
    void CheckTiming(uint32_t loadCWPoints){
        // Every visit to a timed state lasted at least its time
        for (const StateVisit &v : monitor.visits){
            uint32_t minimum = 0;
            switch (v.state){
                case PowerCalSm_StateId_KEYDOWN: minimum = SETTLE_MS; break;
                case PowerCalSm_StateId_COLLECT: minimum = COLLECT_MS; break;
                case PowerCalSm_StateId_REST:    minimum = REST_MS; break;
                case PowerCalSm_StateId_BAND:    minimum = BAND_SETTLE_MS; break;
                default: break;
            }
            if (minimum && (v.longest_ms < minimum)){
                ADD_FAILURE() << PowerCalSm_state_id_to_string(v.state) << " lasted at most "
                              << v.longest_ms << " ms, needs " << minimum;
                break;
            }
        }

        for (int32_t b = BAND_160M; b <= BAND_6M; b++){
            SCOPED_TRACE(bands[b].name);
            // The load: one key-down through all the CW points, then the SSB tone
            std::vector<KeyInterval> load = KeyDowns(b, CONNECT_LOAD);
            if (loadCWPoints > 0){
                ASSERT_EQ(load.size(), 2u) << "stays keyed between load points";
                EXPECT_FALSE(load[0].ssb);
                EXPECT_TRUE(load[1].ssb);
                EXPECT_GE(load[0].longest(), loadCWPoints * (SETTLE_MS + COLLECT_MS));
            } else {
                EXPECT_EQ(load.size(), 0u);
            }
            // The open and short: one key-down per point, short, with a rest between
            for (Connector c : {CONNECT_OPEN, CONNECT_SHORT}){
                SCOPED_TRACE(ConnectorName(c));
                std::vector<KeyInterval> v = KeyDowns(b, c);
                bool oscillates = bench.Oscillates(b, c);
                if (oscillates){
                    // Stuck whatever the drive: abandoned at the first point if it is
                    // far above the 50 Ohm reference, else at the third short fall
                    EXPECT_GE(v.size(), 1u);
                    EXPECT_LE(v.size(), 4u) << "abandoned by the third short fall running";
                } else {
                    EXPECT_EQ(v.size(), 6u);
                }
                for (size_t i = 0; i < v.size(); i++){
                    EXPECT_TRUE(v[i].ssb) << "the open and short use the SSB tone";
                    EXPECT_LE(v[i].shortest(), SETTLE_MS + COLLECT_MS + KEYED_SLACK_MS)
                        << "point " << i << " keyed too long";
                    if (i > 0)
                        EXPECT_GE(v[i].firstOn_ms - v[i - 1].lastOn_ms, REST_MS)
                            << "no rest before point " << i;
                }
            }
        }
        // 4 m and GENERAL have no LPF and are never transmitted on
        for (const KeyInterval &k : monitor.keyDowns)
            EXPECT_GE(k.band, 0) << "transmitted with no calibration band's LPF";
    }

    uint32_t meterReadings = 0;
};

///////////////////////////////////////////////////////////////////////////////
// The tests
///////////////////////////////////////////////////////////////////////////////

/**
 * Level 3 over all 11 bands, through the menus. Radio 2's PA oscillates into
 * the short on 10 m; radio 1's forward detector over-reads in saturation on
 * 10 m. Ends with HOME, back in receive.
 */
TEST_P(PowerAutoCalE2E, Level3FullSessionFromTheMenu) {
    int32_t operatorBand = ED.currentBand[ED.activeVFO];
    int64_t operatorFreq = ED.centerFreq_Hz[ED.activeVFO];

    NavigateToPowerCalibration();
    StartAuto(3);
    Press(MENU_OPTION_SELECT); // start
    RunStandards();
    monitor.Finish();
    EXPECT_EQ(monitor.violations, 0u) << monitor.firstViolation;
    EXPECT_EQ(meterReadings, 11u * 8u) << "8 wattmeter readings per band";

    // The review: every band passed; open alone where the PA oscillates into the short
    for (int32_t b = BAND_160M; b <= BAND_6M; b++){
        const AutoCalBandResult *r = GetAutoCalResult(b);
        EXPECT_TRUE(r->checked) << bands[b].name;
        EXPECT_EQ(r->failBits, 0u) << bands[b].name;
        EXPECT_EQ(r->openOnly, bench.Oscillates(b, CONNECT_SHORT)) << bands[b].name;
    }
    EXPECT_FALSE(GetAutoCalResult(BAND_4M)->inSession);
    EXPECT_FALSE(GetAutoCalResult(BAND_GENERAL)->inSession);
    EXPECT_EQ(GetAutoCalResult(BAND_10M)->ceilingDropped, fabsf(bench.radio->ceilingError_dB) > 0.5f)
        << "a 10 m ceiling read more than 0.5 dB off is left out of the forward fit";

    // Accept
    Press(MENU_OPTION_SELECT);
    ASSERT_TRUE(RunUntilState(PowerCalSm_StateId_DONE));
    Step();
    for (int32_t b = BAND_160M; b <= BAND_6M; b++){
        SCOPED_TRACE(bands[b].name);
        bool openOnly = bench.Oscillates(b, CONNECT_SHORT);
        // The bridge readings are whole mV with a little scatter, so check the
        // stored lines where the radio uses them rather than their intercepts.
        // On 10 m the forward detector is off in saturation; points off by less
        // than the ceiling rule's 0.5 dB stay in the fit, so allow what the
        // calibration doc expects there (Section 2: worst point about 0.26 dB).
        bool tenM = (b == BAND_10M);
        EXPECT_NEAR(ED.SWR_F_SlopeAdj[b], bench.m_f(b) - 25.0f, tenM ? 0.5 : 0.15);
        EXPECT_NEAR(ED.SWR_R_SlopeAdj[b], bench.m_r(b) - 25.0f, tenM ? 0.5 : 0.2);
        EXPECT_LT(ForwardReadError_dB(b), tenM ? 0.3 : 0.08) << "forward power, 0.1 to 15 W";
        // From the open alone the leak is not cancelled: half the gap
        EXPECT_LT(ReflectedReadError_dB(b, openOnly ? bench.gap(b) / 2.0f : 0.0f), tenM ? 0.2 : 0.1)
            << "reflected power, 0.1 to 2 W";
        EXPECT_EQ(ED.SWR_CalLevel[b], openOnly ? (3 | SWR_CAL_OPEN_ONLY) : 3);
        EXPECT_GT(ED.SWR_CalDate[b], 0u);
        EXPECT_NEAR(ED.PowerCal_20W_Psat_mW[b], bench.Psat_mW(b), 0.03f * bench.Psat_mW(b));
        EXPECT_NEAR(ED.PowerCal_20W_kindex[b], bench.k(b), 0.06f * bench.k(b));
        // The tone's transfer is not quite 1 dB per dB (0.91-0.96 measured), so
        // the correction is off by (1 - transfer) of the stored correction's error,
        // plus the forward line's error at the tone's power
        float32_t transferError = (1.0f - AtBand(bench.radio->transfer, b)) *
                                  fabsf(bench.ssbGainFor3W(b) - 0.0f /* stored correction */);
        EXPECT_NEAR(ED.PowerCal_20W_DSP_Gain_correction_dB[b], bench.ssbGainFor3W(b),
                    transferError + (tenM ? 0.15f : 0.08f));
        EXPECT_NEAR(StoredSWR(b, 1.0f / 3.0f), 2.0f, 0.05) << "100 Ohm";
        EXPECT_NEAR(StoredSWR(b, 0.6f), 4.0f, 0.2) << "200 Ohm";
        EXPECT_NEAR(GetAutoCalResult(b)->S50, bench.S50(b), 0.05);
    }
    EXPECT_EQ(ED.SWR_CalLevel[BAND_4M], 0);
    EXPECT_EQ(ED.SWR_CalLevel[BAND_GENERAL], 0);

    // The results were saved: scramble them and read them back
    float32_t saved[BAND_6M + 1];
    for (int32_t b = BAND_160M; b <= BAND_6M; b++){
        saved[b] = ED.SWR_R_Offset[b];
        ED.SWR_R_Offset[b] = 99;
        ED.SWR_CalLevel[b] = 0;
    }
    RestoreDataFromStorage();
    for (int32_t b = BAND_160M; b <= BAND_6M; b++){
        EXPECT_NEAR(ED.SWR_R_Offset[b], saved[b], 1e-3) << bands[b].name;
        EXPECT_NE(ED.SWR_CalLevel[b], 0) << bands[b].name;
    }
    EXPECT_EQ(ED.SWR_CouplerTurns, bench.radio->turns);

    CheckTiming(LEVEL3_CW_POINTS);

    // The SSB tone left the DSP chain at the gain the calibration commanded
    EXPECT_GT(monitor.ssbBlocksAtCommandedGain, 0u) << "no SSB transmit block was checked";
    EXPECT_EQ(monitor.ssbBlocksElsewhere, 0u);

    // HOME: out of calibration, back on the operator's band, in receive
    Press(HOME_SCREEN);
    for (int k = 0; k < 5; k++) Step();
    EXPECT_FALSE(PowerCalAutoActive());
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_FALSE(Transmitting());
    EXPECT_EQ(GET_BIT(hardwareRegister, RXTXBIT), 0);
    EXPECT_EQ(ED.currentBand[ED.activeVFO], operatorBand);
    EXPECT_EQ(ED.centerFreq_Hz[ED.activeVFO], operatorFreq);
    float32_t gain;
    EXPECT_FALSE(GetSSBGainOverride(&gain)) << "the SSB gain is back to normal";
}

/**
 * Level 1: abort with ZOOM while transmitting, start again, a band whose PA
 * oscillates into both the open and the short fails, and RETRY repeats only
 * that band once the fault is cleared.
 */
TEST_P(PowerAutoCalE2E, Level1AbortRetryAndAccept) {
    NavigateToPowerCalibration();
    StartAuto(1);
    Press(MENU_OPTION_SELECT);
    Fit(CONNECT_LOAD);
    ASSERT_TRUE(RunUntil([](){ return Transmitting(); }, 5000));

    // ZOOM while keyed: unkeyed by the same loop(), nothing stored
    Press(ZOOM);
    EXPECT_FALSE(Transmitting()) << "ZOOM must unkey at once";
    EXPECT_FALSE(PowerCalAutoActive());
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
    for (int32_t b = FIRST_BAND; b <= LAST_BAND; b++)
        EXPECT_EQ(ED.SWR_CalLevel[b], 0) << "an aborted session stores nothing";
    for (int k = 0; k < 20; k++){
        Step();
        EXPECT_FALSE(Transmitting());
    }

    // Again, with the 6 m PA oscillating into the open and the short as well
    bench.extraOscillationBand = BAND_6M;
    bench.extraOscillationInto = CONNECT_OPEN;
    bench.extraOscillationBoth = true;
    monitor.Reset();
    StartAuto(1);
    Press(MENU_OPTION_SELECT);
    RunStandards();
    monitor.Finish();
    EXPECT_EQ(monitor.violations, 0u) << monitor.firstViolation;
    EXPECT_EQ(meterReadings, 0u) << "level 1 needs no wattmeter";
    CheckTiming(LEVEL1_CW_POINTS);
    EXPECT_TRUE(GetAutoCalResult(BAND_6M)->failBits & AUTO_FAIL_OPEN);
    EXPECT_EQ(autoCal.failedMask, 1u << BAND_6M) << "only 6 m fails";

    // RETRY (button 14) for 6 m alone, the fault cleared
    Press(14);
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_SETUP);
    EXPECT_EQ(autoCal.bandMask, 1u << BAND_6M);
    bench.extraOscillationBand = -1;
    bench.extraOscillationBoth = false;
    monitor.Reset();
    Press(MENU_OPTION_SELECT);
    RunStandards();
    monitor.Finish();
    EXPECT_EQ(monitor.violations, 0u) << monitor.firstViolation;
    for (const KeyInterval &k : monitor.keyDowns)
        EXPECT_EQ(k.band, BAND_6M) << "the retry transmits on 6 m only";
    EXPECT_EQ(GetAutoCalResult(BAND_6M)->failBits, 0u);
    EXPECT_FALSE(AutoAnyFailed());

    Press(MENU_OPTION_SELECT);
    ASSERT_TRUE(RunUntilState(PowerCalSm_StateId_DONE));
    Step();
    for (int32_t b = BAND_160M; b <= BAND_6M; b++){
        SCOPED_TRACE(bands[b].name);
        bool openOnly = bench.Oscillates(b, CONNECT_SHORT);
        EXPECT_EQ(ED.SWR_CalLevel[b], openOnly ? (1 | SWR_CAL_OPEN_ONLY) : 1)
            << "the first round's passes are kept";
        EXPECT_FLOAT_EQ(ED.SWR_F_SlopeAdj[b], 0.0f) << "nominal slope";
        EXPECT_NEAR(ED.SWR_F_Offset[b], SWRCalLevel1ForwardOffset(b, bench.radio->turns), 1e-4)
            << "the level 1 default for the radio's turns";
        // Section 2: level 1 reads the SWR almost as well as level 3
        EXPECT_NEAR(StoredSWR(b, 1.0f / 3.0f), 2.0f, 0.1) << "100 Ohm";
    }

    // ZOOM from DONE: back to the manual flow, receive-safe
    Press(ZOOM);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1);
    EXPECT_FALSE(Transmitting());
}

INSTANTIATE_TEST_SUITE_P(BenchRadios, PowerAutoCalE2E, ::testing::Values(&RADIO1, &RADIO2),
    [](const ::testing::TestParamInfo<const RadioProfile *> &info){ return std::string(info.param->name); });
