// Tests for the ATU tuning engine (ATUTune.cpp) with a simulated plant: the SWR
// bridge's AD7991 answers from the C model of the ATU, a load, and the relays
// and keying the firmware has set. Built in all_ATU_tests, with ATU_PRESENT.

#include "gtest/gtest.h"

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include "../src/PhoenixSketch/SDT.h"
#include "Adafruit_I2CDevice.h"
#include "atu_plant/ATUModel.h"

extern char *command_parser(char *command);
void AddMillisTime(uint64_t delta_ms);
bool AutoCalAllowed(void);

namespace {

// ------------------------------------------------------------- simulated plant

ATUModelCache plantCache;
atu_cf plantLoad = {50.0f, 0.0f};
float plantFwd_dBm = 33.0f;     // 2 W on the line while the carrier is on
bool plantNoRF = false;         // the PA puts out nothing

int32_t Band(void){ return ED.currentBand[ED.activeVFO]; }

/// The carrier is on: T/R relay in TX and the CW carrier keyed
bool Keyed(void){
    return GET_BIT(hardwareRegister, RXTXBIT) && GET_BIT(hardwareRegister, CWBIT);
}

/// |Gamma| at the bridge for the relays now set
float PlantGamma(void){
    uint32_t s = GetATURegisterState();
    if (!ATU_PACKED_NET(s)){
        float dr = plantLoad.re - 50.0f, sr = plantLoad.re + 50.0f, x = plantLoad.im;
        return sqrtf((dr * dr + x * x) / (sr * sr + x * x));
    }
    return ATUCAbs(ATUModelGamma(&plantCache, ATU_PACKED_CAPSIN(s) ? ATU_TOPO_CL : ATU_TOPO_LC,
                                 ATU_PACKED_L(s), ATU_PACKED_C(s), plantLoad));
}

/// Detector voltage for a line power, inverting the band's bridge calibration
float Volts_mV(float p_dBm, bool forward){
    int32_t b = Band();
    float m = SWRCAL_NOMINAL_SLOPE_MV_PER_DB + (forward ? ED.SWR_F_SlopeAdj[b] : ED.SWR_R_SlopeAdj[b]);
    float off = (forward ? ED.SWR_F_Offset[b] : ED.SWR_R_Offset[b]) - SWRCAL_OFFSET_CONSTANT_DB;
    return (p_dBm - off) * m;
}

uint16_t PlantADC(uint8_t channel){
    float mV;
    if (!Keyed() || plantNoRF){
        mV = (channel == 0) ? 230.0f : 355.0f;     // key-up floor
    } else if (channel == 0){
        mV = Volts_mV(plantFwd_dBm, true);
    } else {
        float g = fmaxf(PlantGamma(), 1e-4f);
        mV = Volts_mV(plantFwd_dBm + 20.0f * log10f(g), false);
    }
    if (mV < 0) mV = 0;
    if (mV > 4095) mV = 4095;
    return (uint16_t)lroundf(mV);
}

std::string Cat(const char *cmd){
    char buf[64];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return std::string(command_parser(buf));
}

}  // namespace

class ATUTuneTest : public ::testing::Test {
protected:
    int32_t savedBand;
    uint8_t savedCal;
    bool relayChangedWhileKeyed;

    void SetUp() override {
        Adafruit_I2CDevice::resetMockState();
        Adafruit_I2CDevice::setMockDevicePresent(true);
        Adafruit_I2CDevice::setMockADCProvider(PlantADC);
        savedBand = ED.currentBand[ED.activeVFO];
        ED.currentBand[ED.activeVFO] = BAND_20M;
        savedCal = ED.SWR_CalLevel[BAND_20M];
        ED.SWR_CalLevel[BAND_20M] = 1;
        InitializeRFHardware();
        ModeSm_start(&modeSM);                  // SSB_RECEIVE
        UpdateRFHardwareState();
        ResetATUBoard();
        ATUModelPrepare(&plantCache, &ATUModelDefaultParams, 14.2e6);
        plantLoad = {50.0f, 0.0f};
        plantFwd_dBm = 33.0f;
        plantNoRF = false;
        relayChangedWhileKeyed = false;
    }

    void TearDown() override {
        if (ATUTuneActive()) ATUTuneAbort();
        Adafruit_I2CDevice::setMockADCProvider(nullptr);
        ED.SWR_CalLevel[BAND_20M] = savedCal;
        ED.currentBand[ED.activeVFO] = savedBand;
        ResetATUBoard();
    }

    /// Run main-loop passes of 5 ms until the program ends; false if it never does
    bool Run(int maxPasses = 5000){
        bool wasKeyed = false;
        uint32_t lastRelays = GetATURegisterState();
        for (int i = 0; i < maxPasses; i++){
            ATUTuneService();
            if (modeSM.state_id == ModeSm_StateId_ATU_TUNE_MARK) PerformSWRBridgeReading();
            bool keyed = Keyed();
            uint32_t relays = GetATURegisterState();
            if (wasKeyed && keyed && (relays != lastRelays)) relayChangedWhileKeyed = true;
            wasKeyed = keyed;
            lastRelays = relays;
            if (!ATUTuneActive()) return true;
            AddMillisTime(5);
        }
        return false;
    }

    /// Run until the carrier is on, then stop
    bool RunUntilKeyed(int maxPasses = 200){
        for (int i = 0; i < maxPasses; i++){
            ATUTuneService();
            if (Keyed()) return true;
            AddMillisTime(5);
        }
        return false;
    }

    void ExpectBackInReceive(ModeSm_StateId rx = ModeSm_StateId_SSB_RECEIVE){
        EXPECT_FALSE(ATUTuneActive());
        EXPECT_EQ(modeSM.state_id, rx);
        EXPECT_FALSE(Keyed());
        EXPECT_EQ(GET_BIT(hardwareRegister, RXTXBIT), 0u);
        EXPECT_FALSE(relayChangedWhileKeyed);
    }
};

TEST_F(ATUTuneTest, ReadProgramReadsBypassThenTheOperatorSetting) {
    plantLoad = {100.0f, 0.0f};                 // |Gamma| = 1/3 straight through
    const uint32_t op = ATU_PACK(1, 0, 0, 0x02, 0x10);
    SetATUPacked(op);
    ASSERT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_OK);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_ATU_TUNE_SPACE);
    ASSERT_TRUE(Run());
    const ATUTuneRecord *r = ATUTuneLastRecord();
    EXPECT_EQ(r->code, ATU_OK);
    ASSERT_EQ(r->n, 6);
    float expectOp = ATUCAbs(ATUModelGamma(&plantCache, ATU_TOPO_LC, 0x02, 0x10, plantLoad));
    for (int i = 0; i < 3; i++){
        EXPECT_EQ(r->setting[i], ATU_PACKED_BYPASS);
        EXPECT_NEAR(r->gamma[i], 1.0f / 3.0f, 0.01f);
        EXPECT_EQ(r->setting[3 + i], op);
        EXPECT_NEAR(r->gamma[3 + i], expectOp, 0.01f);
    }
    EXPECT_NEAR(r->fwd_W, 2.0f, 0.05f);
    EXPECT_EQ(r->finalSetting, op);             // the operator's setting is left in place
    EXPECT_EQ(GetATURegisterState(), op);
    ExpectBackInReceive();
}

TEST_F(ATUTuneTest, ReturnsToCWReceiveWhenStartedThere) {
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    ASSERT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
    ASSERT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_OK);
    ASSERT_TRUE(Run());
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_OK);
    ExpectBackInReceive(ModeSm_StateId_CW_RECEIVE);
}

TEST_F(ATUTuneTest, RefusesWhenItCannotRunSafely) {
    // Bridge not calibrated on this band
    ED.SWR_CalLevel[BAND_20M] = 0;
    EXPECT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_REFUSED_UNCALIBRATED);
    ED.SWR_CalLevel[BAND_20M] = 1;
    // Transmit not allowed on this band
    int32_t nonHam = -1;
    for (int32_t b = 0; b < NUMBER_OF_BANDS; b++)
        if (bands[b].band_type != HAM_BAND){ nonHam = b; break; }
    ASSERT_GE(nonHam, 0);
    ED.currentBand[ED.activeVFO] = nonHam;
    EXPECT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_REFUSED_BAND);
    ED.currentBand[ED.activeVFO] = BAND_20M;
    // Not from transmit
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    ASSERT_EQ(modeSM.state_id, ModeSm_StateId_SSB_TRANSMIT);
    EXPECT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_REFUSED_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
    // Not twice
    ASSERT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_OK);
    EXPECT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_REFUSED_BUSY);
    ATUTuneAbort();
    // Not without the board
    bit_results.ATU_MCP23008_present = false;
    EXPECT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_REFUSED_NO_BOARD);
    bit_results.ATU_MCP23008_present = true;
    ExpectBackInReceive();
}

TEST_F(ATUTuneTest, AbortWhileKeyedUnkeysAndRestoresTheOperatorSetting) {
    const uint32_t op = ATU_PACK(1, 0, 1, 0x20, 0x08);
    SetATUPacked(op);
    ASSERT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_OK);
    ASSERT_TRUE(RunUntilKeyed());
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);   // reading bypass first
    ATUTuneAbort();
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_ABORTED);
    EXPECT_EQ(GetATURegisterState(), op);
    ExpectBackInReceive();
}

TEST_F(ATUTuneTest, FrontPanelActionAbortsAndIsNotActedOn) {
    ASSERT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_OK);
    ASSERT_TRUE(RunUntilKeyed());
    int32_t band = Band();
    SetButton(BAND_UP);
    SetInterrupt(iBUTTON_PRESSED);
    ConsumeInterrupt();
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_ABORTED);
    EXPECT_EQ(Band(), band);                    // the band button was swallowed
    ExpectBackInReceive();
}

TEST_F(ATUTuneTest, NoRFFailsAndRestores) {
    const uint32_t op = ATU_PACK(1, 0, 0, 0x01, 0x01);
    SetATUPacked(op);
    plantNoRF = true;
    ASSERT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_OK);
    ASSERT_TRUE(Run());
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_FAIL_NO_RF);
    EXPECT_EQ(GetATURegisterState(), op);
    ExpectBackInReceive();
}

TEST_F(ATUTuneTest, BridgeAndPowerCalibrationAreLockedOutWhileRunning) {
    ASSERT_EQ(ATUTuneStart(ATU_PROG_READ), ATU_OK);
    EXPECT_EQ(Cat("BR0;"), "?;");
    EXPECT_FALSE(AutoCalAllowed());
    EXPECT_EQ(Cat("AT100000000;"), "?;");      // manual relay control is refused too
    ATUTuneAbort();
    EXPECT_EQ(Cat("BR0;"), "");
}

TEST_F(ATUTuneTest, CatStartsReportsAndStops) {
    plantLoad = {25.0f, 0.0f};
    EXPECT_EQ(Cat("AX2;"), "");
    EXPECT_TRUE(ATUTuneActive());
    ASSERT_TRUE(Run());
    std::string r = Cat("AX;");
    // AX<prog>,<code>,<active>,<n>,...
    EXPECT_EQ(r.substr(0, 9), "AX2,0,0,6");
    EXPECT_NE(r.find(",333,333,333,"), std::string::npos) << r;   // |Gamma| 1/3, x 1000
    EXPECT_EQ(r.back(), ';');

    // AX0 and TX; both stop a running program, and TX; does not key
    EXPECT_EQ(Cat("AX2;"), "");
    EXPECT_EQ(Cat("AX0;"), "");
    EXPECT_FALSE(ATUTuneActive());
    EXPECT_EQ(Cat("AX2;"), "");
    ASSERT_TRUE(RunUntilKeyed());
    EXPECT_EQ(Cat("TX;"), "");
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_ABORTED);
    ExpectBackInReceive();

    // Refusals come back as AX<code>
    ED.SWR_CalLevel[BAND_20M] = 0;
    EXPECT_EQ(Cat("AX2;"), "AX5;");
}

// ====================================================================== tuning

namespace {

ATUModelParams plantParams;
atu_cf bridgeLeak = {0.0f, 0.0f};   // added to every reading, like the bridge's directivity

/// Coils of another build: L and Cp each off by about sigma (deterministic)
void MakePlant(double sigma, uint32_t seed){
    plantParams = ATUModelDefaultParams;
    uint32_t x = seed * 2654435761u + 1;
    auto uni = [&x]() { x = x * 1664525u + 1013904223u; return (x >> 8) / 16777216.0; };
    for (int n = 0; n < ATU_NUM_COILS; n++){
        double g1 = sqrt(-2 * log(uni() + 1e-12)) * cos(2 * M_PI * uni());
        double g2 = sqrt(-2 * log(uni() + 1e-12)) * cos(2 * M_PI * uni());
        plantParams.ind[n].L_nH *= 1 + sigma * g1;
        plantParams.ind[n].Cp_pF *= 1 + sigma * g2;
    }
    // The capacitors' lead inductance differs too, so settings near a series
    // resonance disagree with the model, as they do on the board
    for (int n = 0; n < ATU_NUM_CAPS; n++){
        double g1 = sqrt(-2 * log(uni() + 1e-12)) * cos(2 * M_PI * uni());
        double g2 = sqrt(-2 * log(uni() + 1e-12)) * cos(2 * M_PI * uni());
        plantParams.cap[n].Llead_nH *= 1 + sigma * g1;
        plantParams.cap[n].C_pF *= 1 + 0.4 * sigma * g2;
    }
}

float TrueGammaOf(uint32_t s){
    if (!ATU_PACKED_NET(s)){
        float dr = plantLoad.re - 50.0f, sr = plantLoad.re + 50.0f, x = plantLoad.im;
        return sqrtf((dr * dr + x * x) / (sr * sr + x * x));
    }
    return ATUCAbs(ATUModelGamma(&plantCache, ATU_PACKED_CAPSIN(s) ? ATU_TOPO_CL : ATU_TOPO_LC,
                                 ATU_PACKED_L(s), ATU_PACKED_C(s), plantLoad));
}

uint16_t LeakyPlantADC(uint8_t channel){
    if (!Keyed() || channel == 0) return PlantADC(channel);
    uint32_t s = GetATURegisterState();
    atu_cf g;
    if (!ATU_PACKED_NET(s)){
        float den = (plantLoad.re + 50) * (plantLoad.re + 50) + plantLoad.im * plantLoad.im;
        float nr = plantLoad.re - 50, ni = plantLoad.im, dr = plantLoad.re + 50, di = plantLoad.im;
        g = {(nr * dr + ni * di) / den, (ni * dr - nr * di) / den};
    } else {
        g = ATUModelGamma(&plantCache, ATU_PACKED_CAPSIN(s) ? ATU_TOPO_CL : ATU_TOPO_LC,
                          ATU_PACKED_L(s), ATU_PACKED_C(s), plantLoad);
    }
    g.re += bridgeLeak.re;
    g.im += bridgeLeak.im;
    float mag = fmaxf(ATUCAbs(g), 1e-4f);
    float mV = Volts_mV(plantFwd_dBm + 20.0f * log10f(mag), false);
    return (uint16_t)lroundf(fminf(fmaxf(mV, 0.0f), 4095.0f));
}

float Swr(float g){ g = fminf(g, 0.999f); return (1 + g) / (1 - g); }

}  // namespace

class ATUTuneAlgoTest : public ATUTuneTest {
protected:
    uint8_t savedCal[NUMBER_OF_BANDS];
    void SetUp() override {
        ATUTuneTest::SetUp();
        for (int b = 0; b < NUMBER_OF_BANDS; b++){ savedCal[b] = ED.SWR_CalLevel[b]; ED.SWR_CalLevel[b] = 1; }
        Adafruit_I2CDevice::setMockADCProvider(LeakyPlantADC);
        bridgeLeak = {0.0f, 0.0f};
    }
    void TearDown() override {
        for (int b = 0; b < NUMBER_OF_BANDS; b++) ED.SWR_CalLevel[b] = savedCal[b];
        ATUTuneTest::TearDown();
    }
    /// Tune to a frequency, on a plant whose coils differ from the model
    void SetFrequency(const char *fa){
        Cat(fa);                                // FA echoes the frequency
        ASSERT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
        ASSERT_EQ(GetTXRXFreq_cHz() / 100, atoll(fa + 2)) << fa;
        ATUModelPrepare(&plantCache, &plantParams, (double)GetTXRXFreq_cHz() / 100.0);
    }
};

// ====================================================================== memory

class ATUMemoryTest : public ATUTuneAlgoTest {
protected:
    void SetUp() override {
        ATUTuneAlgoTest::SetUp();
        for (int b = 0; b < NUMBER_OF_BANDS; b++)
            for (int a = 0; a < ATU_NUM_ANTENNAS; a++) ED.ATU_State[b][a] = 0;
        for (int a = 0; a < ATU_NUM_ANTENNAS; a++){ ED.ATU_Enabled[a] = true; ED.ATU_BalunOut[a] = false; }
        ATUMemoryResetHook();
    }
    void TearDown() override {
        for (int a = 0; a < ATU_NUM_ANTENNAS; a++){ ED.ATU_Enabled[a] = true; ED.ATU_BalunOut[a] = false; }
        ED.antennaSelection[BAND_20M] = 0;
        ATUTuneAlgoTest::TearDown();
    }
    void SetBand(int32_t band){
        ED.currentBand[ED.activeVFO] = band;
        UpdateTuneState();
    }
};

TEST_F(ATUMemoryTest, StoreGetForget) {
    uint32_t s; int32_t f; float swr;
    EXPECT_FALSE(ATUMemoryGet(BAND_20M, 0, &s, &f, &swr));
    ATUMemoryStore(BAND_20M, 0, ATU_PACK(1, 0, 1, 12, 34), 14200, 1.15f);
    ASSERT_TRUE(ATUMemoryGet(BAND_20M, 0, &s, &f, &swr));
    EXPECT_EQ(s, ATU_PACK(1, 0, 1, 12, 34));
    EXPECT_EQ(f, 14200);
    EXPECT_FLOAT_EQ(swr, 1.15f);
    EXPECT_FALSE(ATUMemoryGet(BAND_20M, 1, &s, &f, &swr));   // per antenna
    // A stored bypass is a valid entry
    ATUMemoryStore(BAND_40M, 0, ATU_PACKED_BYPASS, 7100, 1.1f);
    ASSERT_TRUE(ATUMemoryGet(BAND_40M, 0, &s, NULL, NULL));
    EXPECT_EQ(s, ATU_PACKED_BYPASS);
    ATUMemoryForget(BAND_20M, 0);
    EXPECT_FALSE(ATUMemoryGet(BAND_20M, 0, &s, &f, &swr));
    EXPECT_FALSE(ATUMemoryGet(-1, 0, &s, &f, &swr));
    EXPECT_FALSE(ATUMemoryGet(BAND_20M, 4, &s, &f, &swr));
    // CAT AX5 forgets the current band and antenna
    ED.currentBand[ED.activeVFO] = BAND_40M;
    EXPECT_EQ(Cat("AX5;"), "");
    EXPECT_FALSE(ATUMemoryGet(BAND_40M, 0, &s, NULL, NULL));
}

TEST_F(ATUMemoryTest, BandChangeAppliesTheStoredSettingOnlyOnChange) {
    const uint32_t x = ATU_PACK(1, 0, 0, 0x21, 0x05);
    ATUMemoryStore(BAND_20M, 0, x, 14200, 1.2f);
    SetBand(BAND_40M);                          // nothing stored: bypass
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);
    SetBand(BAND_20M);
    EXPECT_EQ(GetATURegisterState(), x);
    // A manual change survives later tune updates on the same band
    const uint32_t manual = ATU_PACK(1, 0, 1, 3, 3);
    SetATUPacked(manual);
    UpdateTuneState();
    UpdateTuneState();
    EXPECT_EQ(GetATURegisterState(), manual);
    // An antenna change counts as a change
    ED.antennaSelection[BAND_20M] = 1;
    UpdateTuneState();
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);
    ED.antennaSelection[BAND_20M] = 0;
    UpdateTuneState();
    EXPECT_EQ(GetATURegisterState(), x);
}

TEST_F(ATUMemoryTest, NeverSwitchesWhileTransmittingAndCatchesUpInReceive) {
    const uint32_t x = ATU_PACK(1, 0, 0, 0x08, 0x02);
    ATUMemoryStore(BAND_40M, 0, x, 7100, 1.3f);
    SetBand(BAND_20M);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    ASSERT_EQ(modeSM.state_id, ModeSm_StateId_SSB_TRANSMIT);
    SetBand(BAND_40M);                          // band changes while keyed
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    EXPECT_EQ(GetATURegisterState(), x);        // applied on the way back to receive
}

TEST_F(ATUMemoryTest, ABypassedAntennaStaysBypassedAcrossBandChanges) {
    const uint32_t x = ATU_PACK(1, 0, 0, 1, 1);
    ATUMemoryStore(BAND_20M, 0, x, 14200, 1.2f);
    ATUMemoryStore(BAND_20M, 1, ATU_PACK(1, 0, 1, 2, 2), 14200, 1.2f);
    SetBand(BAND_20M);
    EXPECT_EQ(GetATURegisterState(), x);
    ASSERT_TRUE(ATUSetEnabled(0, false));
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);
    SetBand(BAND_40M);
    SetBand(BAND_20M);
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);
    // Antenna 1 is still enabled
    ED.antennaSelection[BAND_20M] = 1;
    UpdateTuneState();
    EXPECT_EQ(GetATURegisterState(), ATU_PACK(1, 0, 1, 2, 2));
    ED.antennaSelection[BAND_20M] = 0;
    UpdateTuneState();
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);
    // Enabling puts the stored setting back at once
    ASSERT_TRUE(ATUSetEnabled(0, true));
    EXPECT_EQ(GetATURegisterState(), x);
    EXPECT_TRUE(ATUMemoryGet(BAND_20M, 0, NULL, NULL, NULL));   // memory kept throughout
}

TEST_F(ATUMemoryTest, TheBalunOutputIsKeptInBypassAndInStoredSettings) {
    const uint32_t coaxStored = ATU_PACK(1, 0, 0, 4, 4);
    ATUMemoryStore(BAND_20M, 0, coaxStored, 14200, 1.2f);
    SetBand(BAND_20M);
    ASSERT_TRUE(ATUSetBalunOut(0, true));
    EXPECT_EQ(GetATURegisterState(), ATU_PACK(1, 1, 0, 4, 4));
    ASSERT_TRUE(ATUSetEnabled(0, false));
    EXPECT_EQ(GetATURegisterState(), ATU_PACK(0, 1, 0, 0, 0));
    SetBand(BAND_40M);                          // nothing stored there
    EXPECT_EQ(GetATURegisterState(), ATU_PACK(0, 1, 0, 0, 0));
}

TEST_F(ATUMemoryTest, ClearingAnAntennaLeavesTheOthers) {
    ATUMemoryStore(BAND_20M, 0, ATU_PACK(1, 0, 0, 1, 1), 14200, 1.2f);
    ATUMemoryStore(BAND_40M, 0, ATU_PACK(1, 0, 0, 2, 2), 7100, 1.2f);
    ATUMemoryStore(BAND_20M, 1, ATU_PACK(1, 0, 0, 3, 3), 14200, 1.2f);
    SetBand(BAND_20M);
    ASSERT_TRUE(ATUMemoryClearAntenna(0));
    EXPECT_FALSE(ATUMemoryGet(BAND_20M, 0, NULL, NULL, NULL));
    EXPECT_FALSE(ATUMemoryGet(BAND_40M, 0, NULL, NULL, NULL));
    EXPECT_TRUE(ATUMemoryGet(BAND_20M, 1, NULL, NULL, NULL));
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);
    char l1[16], l2[16];
    ATUStatusText(l1, l2, sizeof(l1));
    EXPECT_STREQ(l1, "MEM");
    EXPECT_STREQ(l2, "CLEARED");
    AddMillisTime(ATU_STATUS_HOLD_MS + 1);
    ATUStatusText(l1, l2, sizeof(l1));
    EXPECT_STREQ(l1, "ATU BYP");
}

TEST_F(ATUMemoryTest, MemoryAndSettingsRoundTripThroughStorage) {
    InitializeStorage();
    ATUMemoryStore(BAND_15M, 2, ATU_PACK(1, 0, 1, 200, 17), 21200, 1.44f);
    ED.ATU_TunePower_W = 3.5f;
    ED.ATU_Enabled[1] = false;
    ED.ATU_BalunOut[3] = true;
    SaveDataToStorage(false);
    ED.ATU_State[BAND_15M][2] = 0;
    ED.ATU_Freq_kHz[BAND_15M][2] = 0;
    ED.ATU_SWR[BAND_15M][2] = 0;
    ED.ATU_TunePower_W = 2.0f;
    ED.ATU_Enabled[1] = true;
    ED.ATU_BalunOut[3] = false;
    RestoreDataFromStorage();
    uint32_t s; int32_t f; float swr;
    ASSERT_TRUE(ATUMemoryGet(BAND_15M, 2, &s, &f, &swr));
    EXPECT_EQ(s, ATU_PACK(1, 0, 1, 200, 17));
    EXPECT_EQ(f, 21200);
    EXPECT_NEAR(swr, 1.44f, 1e-4f);
    EXPECT_FLOAT_EQ(ED.ATU_TunePower_W, 3.5f);
    EXPECT_FALSE(ED.ATU_Enabled[1]);
    EXPECT_TRUE(ED.ATU_Enabled[0]);
    EXPECT_TRUE(ED.ATU_BalunOut[3]);
    EXPECT_FALSE(ED.ATU_BalunOut[0]);
    ED.ATU_TunePower_W = 2.0f;
}

TEST_F(ATUMemoryTest, StatusTableRowsSayWhatIsStored) {
    char row[64];
    ATUStatusTableRow(BAND_20M, 0, row, sizeof(row));
    EXPECT_STREQ(row, "20M    --");
    ATUMemoryStore(BAND_20M, 0, ATU_PACK(1, 0, 1, 15, 13), 14200, 1.25f);
    ATUStatusTableRow(BAND_20M, 0, row, sizeof(row));
    EXPECT_STREQ(row, "20M    14200 kHz  CL 0.82uH  157pF    1.25");
    ATUMemoryStore(BAND_40M, 0, ATU_PACKED_BYPASS, 7100, 1.18f);
    ATUStatusTableRow(BAND_40M, 0, row, sizeof(row));
    EXPECT_STREQ(row, "40M     7100 kHz  bypass              1.18");
    ATUMemoryStore(BAND_160M, 0, ATU_PACK(1, 0, 0, 255, 255), 1850, 0.0f);   // stored by hand
    ATUStatusTableRow(BAND_160M, 0, row, sizeof(row));
    EXPECT_STREQ(row, "160M    1850 kHz  LC 17.2uH 3869pF    --");
    ATUStatusTableRow(BAND_20M, 1, row, sizeof(row));                       // other antenna
    EXPECT_STREQ(row, "20M    --");
}

TEST_F(ATUMemoryTest, ARetuneSearchesAfreshAndStoresAgain) {
    MakePlant(0.05, 7);
    SetFrequency("FA00007100000;");
    plantLoad = {120.0f, -60.0f};
    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
    ASSERT_TRUE(Run());
    const ATUTuneRecord *r = ATUTuneLastRecord();
    ASSERT_EQ(r->code, ATU_OK);
    uint32_t s; int32_t f; float swr;
    ASSERT_TRUE(ATUMemoryGet(BAND_40M, 0, &s, &f, &swr));
    EXPECT_EQ(s, r->finalSetting);
    EXPECT_EQ(f, 7100);
    EXPECT_NEAR(swr, Swr(r->finalGamma), 1e-4f);
    // Tuning again searches from bypass; it does not just keep the stored setting
    Cat("FA00007150000;");
    ATUModelPrepare(&plantCache, &plantParams, 7.15e6);
    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
    ASSERT_TRUE(Run());
    r = ATUTuneLastRecord();
    EXPECT_EQ(r->code, ATU_OK);
    EXPECT_GT(r->n, 1);
    EXPECT_EQ(r->setting[0], ATU_PACKED_BYPASS);
    ASSERT_TRUE(ATUMemoryGet(BAND_40M, 0, &s, &f, &swr));
    EXPECT_EQ(s, r->finalSetting);
    EXPECT_EQ(f, 7150);
    ExpectBackInReceive();
}

TEST_F(ATUMemoryTest, TuneOnOneAntennaLeavesTheOtherAntennasAlone) {
    MakePlant(0.05, 8);
    SetFrequency("FA00014200000;");
    const uint32_t onAnt0 = ATU_PACK(1, 0, 0, 0x21, 0x05);
    ATUMemoryStore(BAND_20M, 0, onAnt0, 14200, 1.2f);
    ED.antennaSelection[BAND_20M] = 1;
    UpdateTuneState();
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);   // nothing stored on antenna 1
    plantLoad = {200.0f, 50.0f};
    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
    ASSERT_TRUE(Run());
    const ATUTuneRecord *r = ATUTuneLastRecord();
    ASSERT_EQ(r->code, ATU_OK);
    uint32_t s; int32_t f; float swr;
    ASSERT_TRUE(ATUMemoryGet(BAND_20M, 1, &s, &f, &swr));
    EXPECT_EQ(s, r->finalSetting);
    ASSERT_TRUE(ATUMemoryGet(BAND_20M, 0, &s, &f, &swr));
    EXPECT_EQ(s, onAnt0);                                  // antenna 0 is untouched
    EXPECT_FALSE(ATUMemoryGet(BAND_20M, 2, &s, &f, &swr));
    // Back on antenna 0, its own setting goes on the relays
    ED.antennaSelection[BAND_20M] = 0;
    UpdateTuneState();
    EXPECT_EQ(GetATURegisterState(), onAnt0);
    ExpectBackInReceive();
}

TEST_F(ATUMemoryTest, AStoppedRetuneOfABypassedAntennaLeavesItsStoredSetting) {
    // Tune enables the ATU on a bypassed antenna. If that tune is stopped, the
    // antenna is enabled, so its stored setting belongs on the relays.
    MakePlant(0.05, 9);
    SetFrequency("FA00014200000;");
    plantLoad = {200.0f, 0.0f};
    const uint32_t x = ATU_PACK(1, 0, 1, 0x21, 0x05);
    ATUMemoryStore(BAND_20M, 0, x, 14200, 1.2f);
    ASSERT_TRUE(ATUSetEnabled(0, false));
    ASSERT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);
    ASSERT_EQ(ATURetune(), ATU_OK);
    EXPECT_TRUE(ATUAntennaEnabled(0));
    ASSERT_TRUE(RunUntilKeyed());
    ATUTuneAbort();
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_ABORTED);
    EXPECT_EQ(GetATURegisterState(), x);
    ExpectBackInReceive();
}

TEST_F(ATUMemoryTest, ABandChangeDuringATuneStopsItAndStoresNothing) {
    // CAT can change the band while the tuner runs (the front panel cannot). The
    // readings are then of the wrong band: stop, and set up the new band.
    MakePlant(0.05, 9);
    SetFrequency("FA00014200000;");
    plantLoad = {200.0f, 0.0f};
    const uint32_t on40 = ATU_PACK(1, 0, 0, 0x08, 0x02);
    ATUMemoryStore(BAND_40M, 0, on40, 7100, 1.3f);
    UpdateTuneState();
    ASSERT_EQ(ATURetune(), ATU_OK);
    ASSERT_TRUE(RunUntilKeyed());
    SetBand(BAND_40M);
    ASSERT_TRUE(Run());
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_FAIL_STATE);
    EXPECT_FALSE(ATUMemoryGet(BAND_20M, 0, NULL, NULL, NULL));
    uint32_t s;
    ASSERT_TRUE(ATUMemoryGet(BAND_40M, 0, &s, NULL, NULL));
    EXPECT_EQ(s, on40);
    EXPECT_EQ(GetATURegisterState(), on40);
    ExpectBackInReceive();
}

TEST_F(ATUMemoryTest, ClearingWhileTransmittingBypassesOnTheWayBackToReceive) {
    const uint32_t x = ATU_PACK(1, 0, 0, 0x21, 0x05);
    ATUMemoryStore(BAND_20M, 0, x, 14200, 1.2f);
    UpdateTuneState();
    ASSERT_EQ(GetATURegisterState(), x);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    ASSERT_EQ(modeSM.state_id, ModeSm_StateId_SSB_TRANSMIT);
    EXPECT_TRUE(ATUMemoryClearAntenna(0));
    EXPECT_EQ(GetATURegisterState(), x);        // never switched with RF present
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);
}

// ================================================================ search tuner

namespace {

/// A load at a reflection coefficient
atu_cf LoadAt(float gr, float gi){
    float den = (1 - gr) * (1 - gr) + gi * gi;
    return {50.0f * (1 - gr * gr - gi * gi) / den, 50.0f * 2 * gi / den};
}

/// True if no setting appears twice among the readings
bool NoRepeats(const ATUTuneRecord *r){
    for (int i = 0; i < r->n; i++)
        for (int j = i + 1; j < r->n; j++)
            if (r->setting[i] == r->setting[j]) return false;
    return true;
}

/// The setting with the lowest reading
uint32_t BestReading(const ATUTuneRecord *r){
    int b = 0;
    for (int i = 1; i < r->n; i++) if (r->gamma[i] < r->gamma[b]) b = i;
    return r->setting[b];
}

}  // namespace

TEST_F(ATUTuneAlgoTest, NearMatchStaysBypassed) {
    MakePlant(0.05, 1);
    SetFrequency("FA00014200000;");
    plantLoad = {55.0f, 8.0f};                  // SWR 1.2
    SetATUPacked(ATU_PACK(1, 0, 0, 0x10, 0x10));
    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
    ASSERT_TRUE(Run());
    const ATUTuneRecord *r = ATUTuneLastRecord();
    EXPECT_EQ(r->code, ATU_OK);
    EXPECT_EQ(r->n, 1);
    EXPECT_EQ(r->finalSetting, ATU_PACKED_BYPASS);
    ExpectBackInReceive();
}

TEST_F(ATUTuneAlgoTest, OpenOrShortIsReportedAsAnAntennaFault) {
    MakePlant(0.05, 2);
    SetFrequency("FA00007100000;");
    plantLoad = {0.05f, 0.0f};                  // a short
    const uint32_t op = ATU_PACK(1, 0, 0, 3, 3);
    SetATUPacked(op);
    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
    ASSERT_TRUE(Run());
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_FAIL_ANTENNA);
    EXPECT_EQ(GetATURegisterState(), op);       // the operator's setting is back
    ExpectBackInReceive();
}

TEST_F(ATUTuneAlgoTest, SearchesWithoutRepeatsAndEndsOnTheBestReading) {
    MakePlant(0.12, 3);
    SetFrequency("FA00007100000;");
    plantLoad = {200.0f, -100.0f};
    SetATUPacked(ATU_PACKED_BYPASS);
    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
    ASSERT_TRUE(Run());
    const ATUTuneRecord *r = ATUTuneLastRecord();
    EXPECT_EQ(r->code, ATU_OK);
    EXPECT_GT(r->n, 2);
    EXPECT_TRUE(NoRepeats(r));
    EXPECT_EQ(r->finalSetting, BestReading(r));
    EXPECT_NEAR(Swr(TrueGammaOf(r->finalSetting)), Swr(r->finalGamma), 0.02f);
    ExpectBackInReceive();
}

TEST_F(ATUTuneAlgoTest, NoMatchKeepsTheBestSettingAndSaysSo) {
    // A load this network can't bring below SWR 1.7 on 160 m, that still reads
    // well below a total reflection at bypass (so it isn't an antenna fault)
    MakePlant(0.05, 4);
    SetFrequency("FA00001900000;");
    bool found = false;
    for (float r = 3.0f; (r <= 6.0f) && !found; r += 1.0f){
        plantLoad = {r, 0.0f};
        found = (TrueGammaOf(ATU_PACKED_BYPASS) < 0.95f) &&
                (Swr(ATUModelSearchAll(&plantCache, plantLoad, ATUSharedTopK()).g) > 1.7f);
    }
    ASSERT_TRUE(found);
    const uint32_t op = ATU_PACK(1, 0, 0, 3, 3);
    SetATUPacked(op);
    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
    ASSERT_TRUE(Run());
    const ATUTuneRecord *r = ATUTuneLastRecord();
    EXPECT_EQ(r->code, ATU_FAIL_NO_MATCH);
    EXPECT_EQ(r->finalSetting, BestReading(r));
    EXPECT_NE(r->finalSetting, op);
    EXPECT_LE(r->n, 120);
    ExpectBackInReceive();
    // The home screen shows the setting kept, flagged for a while
    char l1[16], l2[16];
    EXPECT_EQ(ATUStatusText(l1, l2, sizeof(l1)), ATU_TEXT_BUSY);
    EXPECT_NE(std::string(l1), "TUNE");
    AddMillisTime(ATU_STATUS_HOLD_MS + 1);
    EXPECT_EQ(ATUStatusText(l1, l2, sizeof(l1)), ATU_TEXT_NORMAL);
}

namespace {
/// A front-panel button pressed on the home screen
void PressButton(int32_t button){
    uiSM.state_id = UISm_StateId_HOME;
    while (GetInterrupt() != iNONE) {}          // drop anything earlier tests left queued
    SetButton(button);
    SetInterrupt(iBUTTON_PRESSED);
    ConsumeInterrupt();
}
}  // namespace

TEST_F(ATUTuneAlgoTest, Button16StartsARetuneAndStoresIt) {
    MakePlant(0.05, 9);
    SetFrequency("FA00007100000;");
    plantLoad = {200.0f, 0.0f};
    ATUMemoryStore(BAND_40M, 0, ATU_PACK(1, 0, 0, 1, 1), 7100, 1.1f);
    PressButton(ATU_TUNE_BUTTON);
    ASSERT_TRUE(ATUTuneActive());
    ASSERT_TRUE(Run());
    const ATUTuneRecord *r = ATUTuneLastRecord();
    EXPECT_EQ(r->program, ATU_PROG_TUNE);
    EXPECT_EQ(r->code, ATU_OK);
    EXPECT_GT(r->n, 1);                         // a fresh search, not the stored setting
    uint32_t s; int32_t f; float swr;
    ASSERT_TRUE(ATUMemoryGet(BAND_40M, 0, &s, &f, &swr));
    EXPECT_EQ(s, r->finalSetting);
    EXPECT_EQ(GetATURegisterState(), r->finalSetting);
    ExpectBackInReceive();
}

TEST_F(ATUTuneAlgoTest, Button16AgainStopsTheTune) {
    MakePlant(0.05, 10);
    SetFrequency("FA00014200000;");
    plantLoad = {25.0f, 60.0f};
    const uint32_t op = ATU_PACK(1, 0, 1, 7, 9);
    SetATUPacked(op);
    PressButton(ATU_TUNE_BUTTON);
    ASSERT_TRUE(RunUntilKeyed());
    PressButton(ATU_TUNE_BUTTON);
    EXPECT_FALSE(ATUTuneActive());
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_ABORTED);
    EXPECT_EQ(GetATURegisterState(), op);
    ExpectBackInReceive();
    // The press that stopped it did not start another one
    for (int i = 0; i < 50; i++){ ATUTuneService(); AddMillisTime(5); }
    EXPECT_FALSE(ATUTuneActive());
}

TEST_F(ATUTuneAlgoTest, Button16IsRefusedWhenTheTunerCannotRun) {
    SetFrequency("FA00014200000;");
    ED.SWR_CalLevel[BAND_20M] = 0;              // bridge not calibrated on 20 m
    const uint32_t op = ATU_PACK(1, 0, 0, 3, 3);
    SetATUPacked(op);
    PressButton(ATU_TUNE_BUTTON);
    EXPECT_FALSE(ATUTuneActive());
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_REFUSED_UNCALIBRATED);
    EXPECT_EQ(GetATURegisterState(), op);
    ExpectBackInReceive();
}

TEST(ATUBoardNominal, SumsTheDesignValues) {
    EXPECT_FLOAT_EQ(ATUNominalInductance_uH(0), 0.0f);
    EXPECT_FLOAT_EQ(ATUNominalCapacitance_pF(0), 0.0f);
    EXPECT_NEAR(ATUNominalInductance_uH(0xFF), 17.22f, 1e-4f);
    EXPECT_FLOAT_EQ(ATUNominalCapacitance_pF(0xFF), 3869.0f);
    EXPECT_NEAR(ATUNominalInductance_uH(0x21), 0.05f + 2.2f, 1e-5f);
    EXPECT_FLOAT_EQ(ATUNominalCapacitance_pF(0x05), 10.0f + 47.0f);
}

TEST_F(ATUTuneAlgoTest, StatusTextShowsTheSettingAndTheTuner) {
    char l1[16], l2[16];
    SetFrequency("FA00014200000;");
    AddMillisTime(ATU_STATUS_HOLD_MS + 1);      // nothing recent from earlier tests
    SetATUPacked(ATU_PACKED_BYPASS);
    EXPECT_EQ(ATUStatusText(l1, l2, sizeof(l1)), ATU_TEXT_NORMAL);
    EXPECT_STREQ(l1, "ATU BYP");
    EXPECT_STREQ(l2, "");
    SetATUPacked(ATU_PACK(1, 0, 0, 0x21, 0x0F));
    EXPECT_EQ(ATUStatusText(l1, l2, sizeof(l1)), ATU_TEXT_NORMAL);
    EXPECT_STREQ(l1, "LC 2.25uH");
    EXPECT_STREQ(l2, "   179pF");
    SetATUPacked(ATU_PACK(1, 0, 1, 0xFF, 0xFF));
    ATUStatusText(l1, l2, sizeof(l1));
    EXPECT_STREQ(l1, "CL 17.2uH");
    EXPECT_STREQ(l2, "   3869pF");
    // A refusal shows its reason for a while, then the setting again
    ED.SWR_CalLevel[BAND_20M] = 0;
    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_REFUSED_UNCALIBRATED);
    EXPECT_EQ(ATUStatusText(l1, l2, sizeof(l1)), ATU_TEXT_ALERT);
    EXPECT_STREQ(l1, "TUNE");
    EXPECT_STREQ(l2, "NO CAL");
    AddMillisTime(ATU_STATUS_HOLD_MS + 1);
    EXPECT_EQ(ATUStatusText(l1, l2, sizeof(l1)), ATU_TEXT_NORMAL);
    EXPECT_STREQ(l1, "CL 17.2uH");
    ED.SWR_CalLevel[BAND_20M] = 1;
    // While tuning: the readings so far
    MakePlant(0.05, 11);
    plantLoad = {200.0f, 0.0f};
    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
    ASSERT_TRUE(RunUntilKeyed());
    EXPECT_EQ(ATUStatusText(l1, l2, sizeof(l1)), ATU_TEXT_BUSY);
    EXPECT_STREQ(l1, "TUNING");
    EXPECT_STREQ(l2, "n 0");
    ASSERT_TRUE(Run());
    ASSERT_EQ(ATUTuneLastRecord()->code, ATU_OK);
    EXPECT_EQ(ATUStatusText(l1, l2, sizeof(l1)), ATU_TEXT_NORMAL);
    EXPECT_EQ(std::string(l1).substr(0, 3), "LC ");
}

TEST_F(ATUTuneAlgoTest, CatACStartsStopsAndBypassesTheTuner) {
    MakePlant(0.05, 12);
    SetFrequency("FA00014200000;");
    ED.antennaSelection[BAND_20M] = 0;
    ED.ATU_Enabled[0] = true;
    plantLoad = {25.0f, 60.0f};
    SetATUPacked(ATU_PACKED_BYPASS);
    EXPECT_EQ(Cat("AC;"), "AC010;");            // enabled, not tuning
    // P3 = 1 starts a retune, reported as tuning; P3 = 0 stops it
    EXPECT_EQ(Cat("AC011;"), "");
    EXPECT_TRUE(ATUTuneActive());
    EXPECT_EQ(Cat("AC;"), "AC011;");
    ASSERT_TRUE(RunUntilKeyed());
    EXPECT_EQ(Cat("AC010;"), "");
    EXPECT_FALSE(ATUTuneActive());
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_ABORTED);
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);
    ExpectBackInReceive();
    // P2 = 0 bypasses the ATU on this antenna, keeping the memory; P2 = 1 enables it
    const uint32_t stored = ATU_PACK(1, 0, 1, 0x12, 0x34);
    ATUMemoryStore(BAND_20M, 0, stored, 14200, 1.3f);
    SetATUPacked(stored);
    EXPECT_EQ(Cat("AC100;"), "");
    EXPECT_FALSE(ED.ATU_Enabled[0]);
    EXPECT_EQ(GetATURegisterState(), ATU_PACKED_BYPASS);
    EXPECT_EQ(Cat("AC;"), "AC000;");
    EXPECT_EQ(Cat("AC010;"), "");
    EXPECT_TRUE(ED.ATU_Enabled[0]);
    EXPECT_EQ(GetATURegisterState(), stored);
    // A retune while bypassed enables the ATU again
    EXPECT_EQ(Cat("AC000;"), "");
    EXPECT_EQ(Cat("AC001;"), "");
    EXPECT_TRUE(ED.ATU_Enabled[0]);
    ASSERT_TRUE(Run());
    // Refused tune and malformed commands; a refused tune doesn't enable
    EXPECT_EQ(Cat("AC000;"), "");
    ED.SWR_CalLevel[BAND_20M] = 0;
    EXPECT_EQ(Cat("AC011;"), "?;");
    EXPECT_FALSE(ED.ATU_Enabled[0]);
    ED.SWR_CalLevel[BAND_20M] = 1;
    EXPECT_EQ(Cat("AC200;"), "?;");
    EXPECT_EQ(Cat("AC0x0;"), "?;");
    // Not while transmitting
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    EXPECT_EQ(Cat("AC010;"), "?;");
    EXPECT_FALSE(ED.ATU_Enabled[0]);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
    ExpectBackInReceive();
    ED.ATU_Enabled[0] = true;
    ATUMemoryForget(BAND_20M, 0);
}

TEST_F(ATUTuneAlgoTest, OnTheBalunOutputEveryReadingKeepsIt) {
    MakePlant(0.05, 13);
    SetFrequency("FA00007100000;");
    ED.antennaSelection[BAND_40M] = 0;
    ED.ATU_BalunOut[0] = true;
    plantLoad = {200.0f, -100.0f};
    ASSERT_EQ(ATURetune(), ATU_OK);
    ASSERT_TRUE(Run());
    const ATUTuneRecord *r = ATUTuneLastRecord();
    EXPECT_EQ(r->code, ATU_OK);
    ASSERT_GT(r->n, 2);
    for (int i = 0; i < r->n; i++) EXPECT_TRUE(ATU_PACKED_BAL(r->setting[i])) << i;
    EXPECT_TRUE(ATU_PACKED_BAL(GetATURegisterState()));
    // The status line says so when bypassed
    char l1[16], l2[16];
    AddMillisTime(ATU_STATUS_HOLD_MS + 1);
    SetATUPacked(ATUBypassFor(0));
    ATUStatusText(l1, l2, sizeof(l1));
    EXPECT_STREQ(l1, "ATU BYP");
    EXPECT_STREQ(l2, "BALUN");
    ED.ATU_Enabled[0] = false;
    ATUStatusText(l1, l2, sizeof(l1));
    EXPECT_STREQ(l1, "ATU OFF");
    ED.ATU_Enabled[0] = true;
    ED.ATU_BalunOut[0] = false;
    ExpectBackInReceive();
}

TEST_F(ATUTuneAlgoTest, AbortMidSearchUnkeysAndRestoresTheOperatorSetting) {
    MakePlant(0.05, 5);
    SetFrequency("FA00014200000;");
    plantLoad = {25.0f, 60.0f};
    const uint32_t op = ATU_PACK(1, 0, 1, 7, 9);
    SetATUPacked(op);
    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
    // Run until ten readings are in and the carrier is on again
    for (int i = 0; (i < 5000) && (ATUTuneLastRecord()->n < 10); i++){
        ATUTuneService();
        if (modeSM.state_id == ModeSm_StateId_ATU_TUNE_MARK) PerformSWRBridgeReading();
        AddMillisTime(5);
    }
    ASSERT_TRUE(RunUntilKeyed());
    ATUTuneAbort();
    EXPECT_EQ(ATUTuneLastRecord()->code, ATU_ABORTED);
    EXPECT_EQ(GetATURegisterState(), op);
    ExpectBackInReceive();
}

TEST_F(ATUTuneAlgoTest, CatAYPagesThroughTheReadings) {
    MakePlant(0.05, 6);
    SetFrequency("FA00007100000;");
    plantLoad = {12.5f, 0.0f};
    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
    ASSERT_TRUE(Run());
    const ATUTuneRecord *r = ATUTuneLastRecord();
    ASSERT_GT(r->n, 12);
    // AYpp,<n>,<nbsLLLCCC>:<g x 1000>,... with 12 readings a page
    std::string p0 = Cat("AY00;"), p1 = Cat("AY01;");
    char head[16];
    snprintf(head, sizeof(head), "AY00,%d,", (int)r->n);
    EXPECT_EQ(p0.rfind(head, 0), 0u) << p0;
    char first[32];
    uint32_t s = r->setting[12];
    snprintf(first, sizeof(first), "AY01,%d,%d%d%d%03d%03d:%d", (int)r->n,
             (int)ATU_PACKED_NET(s), (int)ATU_PACKED_BAL(s), (int)ATU_PACKED_CAPSIN(s),
             (int)ATU_PACKED_L(s), (int)ATU_PACKED_C(s), (int)lroundf(r->gamma[12] * 1000.0f));
    EXPECT_EQ(p1.rfind(first, 0), 0u) << p1;
    int colons = 0;
    for (char ch : p0) colons += (ch == ':');
    EXPECT_EQ(colons, 12);
    EXPECT_EQ(p0.back(), ';');
    char past[8];
    snprintf(past, sizeof(past), "AY%02d;", r->n / 12 + 1);
    snprintf(head, sizeof(head), "AY%02d,%d;", r->n / 12 + 1, (int)r->n);
    EXPECT_EQ(Cat(past), head);                 // a page past the end is empty
    EXPECT_EQ(Cat("AYx1;"), "?;");
}

TEST_F(ATUTuneAlgoTest, TunesOtherBuildsCloseToTheirBestOnEveryBand) {
    // Each build's coils and capacitors differ from the reference board; the
    // tuner doesn't know the board at all. "Shortfall" is how far the tuned SWR
    // is above the better of the best the build allows and SRCH_GOOD_SWR (1.2),
    // where the search is allowed to stop.
    //
    // 80-15 m are judged against the best setting of the simulated board. On 10 m
    // that best usually relies on the largest elements far past their resonance,
    // where the model is known to be wrong (code/docs/ATU_RF_Board_Characterisation.md
    // section 3.5), so 10 m only has to beat bypass; the bench judges it.
    //
    // The limits hold the performance this search reached when it was written
    // (5 % build: 50/51 reachable loads within SWR 1.5, 90th percentile shortfall
    // 0.18; 12 % build with a leaky bridge: 46/51, 0.56; median 55-56 readings),
    // so a change that makes it worse fails here.
    struct Variant { const char *name; double sigma; float leakHF, leak10m; float minReached, maxP90; };
    const Variant variants[] = {{"5 % build, no leak", 0.05, 0.0f, 0.0f, 0.9f, 0.3f},
                                {"12 % build, leaky bridge", 0.12, 0.03f, 0.10f, 0.85f, 0.6f}};
    const char *bands[] = {"FA00003650000;", "FA00007100000;", "FA00014200000;",
                           "FA00021200000;", "FA00028400000;"};
    const float R[] = {12.5f, 25.0f, 100.0f, 200.0f, 400.0f};
    const float X[] = {-100.0f, 0.0f, 100.0f};
    int build = 0;
    for (const Variant &v : variants){
        std::cout << "  " << v.name << std::endl;
        std::vector<float> hfShortfall, allReadings;
        int hfReachable = 0, hfReached = 0;
        for (const char *fa : bands){
            std::vector<float> shortfall, readings;
            int reachable = 0, reached = 0;
            float worstSwr = 0;
            bool tenMetres = atoll(fa + 2) > 25000000;
            float leak = tenMetres ? v.leak10m : v.leakHF;
            bridgeLeak = {leak * cosf(1.0f), leak * sinf(1.0f)};
            for (float r : R){
                for (float x : X){
                    MakePlant(v.sigma, 200 + build++);
                    SetFrequency(fa);
                    plantLoad = {r, x};
                    SetATUPacked(ATU_PACKED_BYPASS);
                    ATUMemoryForget(Band(), 0);     // each load is tuned afresh
                    ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
                    ASSERT_TRUE(Run());
                    const ATUTuneRecord *rec = ATUTuneLastRecord();
                    // An antenna fault is fair for a load beyond SWR 10 that a leaky
                    // bridge reads as a nearly total reflection
                    bool fault = (rec->code == ATU_FAIL_ANTENNA) && (Swr(TrueGammaOf(ATU_PACKED_BYPASS)) > 10.0f);
                    ASSERT_TRUE((rec->code == ATU_OK) || (rec->code == ATU_FAIL_NO_MATCH) || fault)
                        << fa << " " << r << (x < 0 ? "" : "+") << x << "j code " << rec->code;
                    if (fault) SetATUPacked(ATU_PACKED_BYPASS);
                    ExpectBackInReceive();
                    float got = Swr(TrueGammaOf(GetATURegisterState()));
                    float best = Swr(ATUModelSearchAll(&plantCache, plantLoad, ATUSharedTopK()).g);
                    shortfall.push_back(fmaxf(0.0f, got - fmaxf(best, 1.2f)));
                    // ATU_DIAG=1 lists the poor results, ATU_DIAG=2 also every reading
                    if (getenv("ATU_DIAG") && (got - fmaxf(best, 1.2f) > 0.3f)){
                        ATUCandidate bc = ATUModelSearchAll(&plantCache, plantLoad, ATUSharedTopK());
                        uint32_t fs = GetATURegisterState();
                        printf("      BAD %s R%.1f X%.0f got %.2f (%s L%d C%d) best %.2f (%s L%d C%d) n=%d\n", fa + 2, r, x, got,
                               ATU_PACKED_CAPSIN(fs) ? "CL" : "LC", ATU_PACKED_L(fs), ATU_PACKED_C(fs), best,
                               bc.topo ? "CL" : "LC", bc.l, bc.c, rec->n);
                        if (getenv("ATU_DIAG")[0] == '2') for (int i = 0; i < rec->n; i++){
                            uint32_t q = rec->setting[i];
                            printf("        %2d %s L%3d C%3d g %.3f true %.3f\n", i, ATU_PACKED_NET(q) ? (ATU_PACKED_CAPSIN(q) ? "CL" : "LC") : "BY",
                                   ATU_PACKED_L(q), ATU_PACKED_C(q), rec->gamma[i], TrueGammaOf(q));
                        }
                    }
                    readings.push_back(rec->n);
                    allReadings.push_back(rec->n);
                    if (tenMetres){
                        float bypass = Swr(TrueGammaOf(ATU_PACKED_BYPASS));
                        EXPECT_LE(got, bypass + 0.01f) << fa << " " << r << (x < 0 ? "" : "+") << x << "j";
                    } else {
                        hfShortfall.push_back(fmaxf(0.0f, got - fmaxf(best, 1.2f)));
                    }
                    worstSwr = fmaxf(worstSwr, got);
                    if (best <= 1.4f){
                        reachable++;
                        reached += (got <= 1.5f);
                        if (!tenMetres){ hfReachable++; hfReached += (got <= 1.5f); }
                    }
                    EXPECT_LE(rec->n, 120);
                }
            }
            std::sort(shortfall.begin(), shortfall.end());
            std::sort(readings.begin(), readings.end());
            size_t n = shortfall.size();
            float median = shortfall[n / 2], p90 = shortfall[(n * 9) / 10];
            std::cout << "    " << (fa + 2) << " median shortfall " << median << ", 90th " << p90
                      << ", worst " << shortfall.back() << ", worst SWR " << worstSwr
                      << ", reached 1.5 in " << reached << "/" << reachable
                      << ", readings median " << readings[n / 2] << " max " << readings.back()
                      << std::endl;
        }
        std::sort(hfShortfall.begin(), hfShortfall.end());
        std::sort(allReadings.begin(), allReadings.end());
        size_t n = hfShortfall.size();
        float median = hfShortfall[n / 2], p90 = hfShortfall[(n * 9) / 10];
        std::cout << "    80-15 m: median shortfall " << median << ", 90th " << p90
                  << ", reached 1.5 in " << hfReached << "/" << hfReachable
                  << "; readings median " << allReadings[allReadings.size() / 2] << std::endl;
        EXPECT_LE(median, 0.05f) << v.name;
        EXPECT_LE(p90, v.maxP90) << v.name;
        EXPECT_GE(hfReached, (int)ceilf(v.minReached * hfReachable)) << v.name;
        EXPECT_LE(allReadings[allReadings.size() / 2], 65.0f) << v.name;
    }
}

// ============================================================ document figures

// Not a check: writes the data behind the figures of code/docs/ATU_Tuning_Algorithm.html.
// Skipped unless ATU_DOC_DIR names a directory. Then, from code/:
//   ATU_DOC_DIR=/tmp/atudoc test/build/all_ATU_tests --gtest_filter='*DocFigureData'
//   python3 tools/atu_vna/build_tuning_doc.py /tmp/atudoc docs/ATU_Tuning_Algorithm.html
// traces.json: each load's tune, every reading as
//   [net, capsIn, L, C, phase (ATUTuneAlgoPhase()), measured |Gamma|, true Gamma re, im]
// <name>.bin: true Gamma (two float32) of every setting, [topology][L][C]
TEST_F(ATUTuneAlgoTest, DocFigureData) {
    const char *dir = getenv("ATU_DOC_DIR");
    if (!dir) GTEST_SKIP() << "set ATU_DOC_DIR to write the figure data";
    struct Case { const char *name; const char *fa; float r, x; };
    const Case cases[] = {{"20m_200", "FA00014200000;", 200, 0}, {"80m_12", "FA00003650000;", 12.5f, 0},
                          {"40m_25p100", "FA00007100000;", 25, 100}, {"15m_100m100", "FA00021200000;", 100, -100},
                          {"40m_400", "FA00007100000;", 400, 0}, {"20m_25m100", "FA00014200000;", 25, -100}};
    FILE *js = fopen((std::string(dir) + "/traces.json").c_str(), "w");
    ASSERT_NE(js, nullptr) << dir;
    fprintf(js, "[\n");
    int k = 0;
    for (const Case &cs : cases){
        MakePlant(0.05, 300 + k);
        SetFrequency(cs.fa);
        plantLoad = {cs.r, cs.x};
        SetATUPacked(ATU_PACKED_BYPASS);
        ATUMemoryForget(Band(), 0);
        ASSERT_EQ(ATUTuneStart(ATU_PROG_TUNE), ATU_OK);
        // As Run(), noting the search phase that asked for each reading. A reading
        // enters the record before the planner is asked for the next step, so the
        // phase on the pass where the record grows is the one that asked for it.
        int phase[ATU_MAX_READINGS] = {};
        int seen = 0;
        for (int i = 0; (i < 5000) && ATUTuneActive(); i++){
            ATUTuneService();
            if (modeSM.state_id == ModeSm_StateId_ATU_TUNE_MARK) PerformSWRBridgeReading();
            while (seen < ATUTuneLastRecord()->n) phase[seen++] = ATUTuneAlgoPhase();
            AddMillisTime(5);
        }
        ASSERT_FALSE(ATUTuneActive());
        const ATUTuneRecord *rec = ATUTuneLastRecord();
        ATUCandidate best = ATUModelSearchAll(&plantCache, plantLoad, ATUSharedTopK());
        fprintf(js, "%s{\"name\":\"%s\",\"f\":%lld,\"r\":%g,\"x\":%g,\"code\":%d,\"final\":%lu,\"finalG\":%g,"
                "\"best\":[%d,%d,%d,%g],\"readings\":[", k ? "," : "", cs.name, (long long)(GetTXRXFreq_cHz() / 100), cs.r, cs.x,
                (int)rec->code, (unsigned long)rec->finalSetting, rec->finalGamma, best.topo, best.l, best.c, best.g);
        for (int i = 0; i < rec->n; i++){
            uint32_t q = rec->setting[i];
            atu_cf g;
            if (!ATU_PACKED_NET(q)){
                float dr = cs.r + 50, nr = cs.r - 50, x = cs.x, den = dr * dr + x * x;
                g = {(nr * dr + x * x) / den, (x * dr - nr * x) / den};
            } else {
                g = ATUModelGamma(&plantCache, ATU_PACKED_CAPSIN(q) ? ATU_TOPO_CL : ATU_TOPO_LC,
                                  ATU_PACKED_L(q), ATU_PACKED_C(q), plantLoad);
            }
            fprintf(js, "%s[%d,%d,%d,%d,%d,%.5f,%.5f,%.5f]", i ? "," : "", (int)ATU_PACKED_NET(q), (int)ATU_PACKED_CAPSIN(q),
                    (int)ATU_PACKED_L(q), (int)ATU_PACKED_C(q), phase[i], rec->gamma[i], g.re, g.im);
        }
        fprintf(js, "]}\n");
        FILE *bf = fopen((std::string(dir) + "/" + cs.name + ".bin").c_str(), "wb");
        ASSERT_NE(bf, nullptr);
        for (int t = 0; t < 2; t++) for (int l = 0; l < 256; l++) for (int c = 0; c < 256; c++){
            atu_cf g = ATUModelGamma(&plantCache, t, l, c, plantLoad);
            fwrite(&g, sizeof(g), 1, bf);
        }
        fclose(bf);
        k++;
    }
    fprintf(js, "]\n");
    fclose(js);
}
