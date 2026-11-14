// Tests for the ATU tuning engine (ATUTune.cpp) with a simulated plant: the SWR
// bridge's AD7991 answers from the C model of the ATU, a load, and the relays
// and keying the firmware has set. Built in all_ATU_tests, with ATU_PRESENT.

#include "gtest/gtest.h"

#include <cmath>
#include <string>
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
        ED.ATU_AutoApply = true;
        ATUMemoryResetHook();
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

TEST_F(ATUMemoryTest, AutoApplyOffLeavesTheRelaysAlone) {
    ATUMemoryStore(BAND_20M, 0, ATU_PACK(1, 0, 0, 1, 1), 14200, 1.2f);
    ED.ATU_AutoApply = false;
    const uint32_t manual = ATU_PACK(1, 0, 1, 7, 7);
    SetATUPacked(manual);
    SetBand(BAND_40M);
    SetBand(BAND_20M);
    EXPECT_EQ(GetATURegisterState(), manual);
}

TEST_F(ATUMemoryTest, MemoryAndSettingsRoundTripThroughStorage) {
    InitializeStorage();
    ATUMemoryStore(BAND_15M, 2, ATU_PACK(1, 0, 1, 200, 17), 21200, 1.44f);
    ED.ATU_TunePower_W = 3.5f;
    ED.ATU_AutoApply = false;
    SaveDataToStorage(false);
    ED.ATU_State[BAND_15M][2] = 0;
    ED.ATU_Freq_kHz[BAND_15M][2] = 0;
    ED.ATU_SWR[BAND_15M][2] = 0;
    ED.ATU_TunePower_W = 2.0f;
    ED.ATU_AutoApply = true;
    RestoreDataFromStorage();
    uint32_t s; int32_t f; float swr;
    ASSERT_TRUE(ATUMemoryGet(BAND_15M, 2, &s, &f, &swr));
    EXPECT_EQ(s, ATU_PACK(1, 0, 1, 200, 17));
    EXPECT_EQ(f, 21200);
    EXPECT_NEAR(swr, 1.44f, 1e-4f);
    EXPECT_FLOAT_EQ(ED.ATU_TunePower_W, 3.5f);
    EXPECT_FALSE(ED.ATU_AutoApply);
    ED.ATU_TunePower_W = 2.0f;
    ED.ATU_AutoApply = true;
}
