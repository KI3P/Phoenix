/**
 * Tests for the transmit power meter (PowerMeter.cpp): PEP and average power on
 * modulated signals, the bridge correction and the SWR from linear averages.
 */
#include "gtest/gtest.h"
#include "../src/PhoenixSketch/SDT.h"

#include <cmath>
#include <vector>

namespace {

const uint32_t BLOCK = 256;
const float32_t FS24 = 24000.0f;
const uint32_t BLOCK_MS_X100 = 1067;   // 256 samples at 24 kHz: 10.67 ms

/** Feed constant-power digital blocks from t0 to t1 (ms); returns the time reached */
uint32_t FeedConstant(float32_t P_W, uint32_t t0_ms, uint32_t t1_ms){
    uint32_t t100 = t0_ms * 100;
    while (t100 / 100 < t1_ms){
        PowerMeterAddDigitalBlock(P_W, P_W * BLOCK, BLOCK, t100 / 100);
        t100 += BLOCK_MS_X100;
    }
    return t100 / 100;
}

/** Feed constant digital blocks and bridge samples (every 16 ms) at a ratio */
uint32_t FeedWithBridge(float32_t P_W, float32_t bridgeRatio, float32_t gamma2,
                        uint32_t t0_ms, uint32_t t1_ms){
    uint32_t t100 = t0_ms * 100;
    uint32_t nextBridge = t0_ms;
    while (t100 / 100 < t1_ms){
        uint32_t t = t100 / 100;
        PowerMeterAddDigitalBlock(P_W, P_W * BLOCK, BLOCK, t);
        while (nextBridge <= t){
            PowerMeterAddBridgeSample(P_W * bridgeRatio, P_W * bridgeRatio * gamma2, nextBridge);
            nextBridge += 16;
        }
        t100 += BLOCK_MS_X100;
    }
    return t100 / 100;
}

float32_t dB(float32_t a, float32_t b){ return 10.0f * log10f(a / b); }

}  // namespace

class PowerMeterTest : public ::testing::Test {
protected:
    void SetUp() override {
        PowerMeterReset();
        PowerMeterSetContext(5, false);
    }
};

TEST_F(PowerMeterTest, NothingBeforeAnySamples){
    EXPECT_FALSE(PowerMeterHasDigital(1000));
    EXPECT_FLOAT_EQ(PowerMeterPEP_W(1000), 0.0f);
    EXPECT_FLOAT_EQ(PowerMeterAvg_W(1000), 0.0f);
    EXPECT_FLOAT_EQ(PowerMeterSWR(1000), 1.0f);
}

TEST_F(PowerMeterTest, DigitalScaleMapsTheCalibrationToneToTheCalibrationPoint){
    // The calibration tone's envelope at the node is the ceiling; after TXGain at
    // the calibration point the amplitude is ceiling * 10^(corr/20).
    const float32_t ceiling = 0.1295f, corr = -2.7f, calPoint = 3.0f;
    float32_t a = ceiling * powf(10.0f, corr / 20.0f);
    std::vector<float32_t> I(BLOCK), Q(BLOCK);
    for (uint32_t k = 0; k < BLOCK; k++){
        float32_t ph = 2.0f * (float32_t)M_PI * 800.0f * k / FS24;
        I[k] = a * cosf(ph);
        Q[k] = a * sinf(ph);
    }
    float32_t peak, sum;
    PowerMeterBlockPower(I.data(), Q.data(), BLOCK, PowerMeterDigitalScale(calPoint, corr, ceiling), &peak, &sum);
    EXPECT_NEAR(peak, calPoint, 1e-3f);
    EXPECT_NEAR(sum / BLOCK, calPoint, 1e-3f);
}

TEST_F(PowerMeterTest, SteadyCarrierPEPEqualsAverage){
    uint32_t t = FeedConstant(50.0f, 1000, 3000);
    EXPECT_TRUE(PowerMeterHasDigital(t));
    EXPECT_NEAR(PowerMeterPEP_W(t), 50.0f, 1e-3f);
    EXPECT_NEAR(PowerMeterAvg_W(t), 50.0f, 1e-3f);
}

TEST_F(PowerMeterTest, TwoTonesPEPIsTwiceTheAverage){
    // Two equal tones, 700 and 1900 Hz, through the block calculation
    const float32_t a = 0.05f, scale = 10000.0f;   // PEP = (2a)^2 * scale = 100 W
    std::vector<float32_t> I(BLOCK), Q(BLOCK);
    uint32_t n = 0;
    uint32_t t100 = 100000;
    for (int blk = 0; blk < 250; blk++){
        for (uint32_t k = 0; k < BLOCK; k++, n++){
            float32_t p1 = 2.0f * (float32_t)M_PI * 700.0f * n / FS24;
            float32_t p2 = 2.0f * (float32_t)M_PI * 1900.0f * n / FS24;
            I[k] = a * (cosf(p1) + cosf(p2));
            Q[k] = a * (sinf(p1) + sinf(p2));
        }
        float32_t peak, sum;
        PowerMeterBlockPower(I.data(), Q.data(), BLOCK, scale, &peak, &sum);
        PowerMeterAddDigitalBlock(peak, sum, BLOCK, t100 / 100);
        t100 += BLOCK_MS_X100;
    }
    uint32_t t = t100 / 100;
    EXPECT_NEAR(PowerMeterPEP_W(t), 100.0f, 1.0f);
    EXPECT_NEAR(PowerMeterAvg_W(t), 50.0f, 0.5f);
    EXPECT_NEAR(dB(PowerMeterPEP_W(t), PowerMeterAvg_W(t)), 3.01f, 0.05f);
}

TEST_F(PowerMeterTest, SyllablesAverageIsDutyTimesPEP){
    // 250 ms of 40 W, 250 ms of silence, repeated: AVG 20 W, PEP 40 W
    uint32_t t = 1000;
    for (int i = 0; i < 8; i++){
        t = FeedConstant(40.0f, t, t + 250);
        t = FeedConstant(0.0f, t, t + 250);
    }
    EXPECT_NEAR(PowerMeterPEP_W(t), 40.0f, 1e-3f);
    EXPECT_NEAR(PowerMeterAvg_W(t), 20.0f, 1.0f);
}

TEST_F(PowerMeterTest, PEPHoldsThenFalls){
    uint32_t t = FeedConstant(80.0f, 1000, 1500);
    t = FeedConstant(10.0f, t, 2700);
    EXPECT_NEAR(PowerMeterPEP_W(t), 80.0f, 1e-3f);    // 1.2 s after the peak: held
    t = FeedConstant(10.0f, t, 3200);
    EXPECT_NEAR(PowerMeterPEP_W(t), 10.0f, 1e-3f);    // 1.7 s after: gone
}

TEST_F(PowerMeterTest, ReadingsAgeOutAfterTransmit){
    uint32_t t = FeedConstant(50.0f, 1000, 2000);
    EXPECT_TRUE(PowerMeterHasDigital(t));
    EXPECT_FALSE(PowerMeterHasDigital(t + 1700));
    EXPECT_FLOAT_EQ(PowerMeterPEP_W(t + 1700), 0.0f);
    EXPECT_FLOAT_EQ(PowerMeterAvg_W(t + 1700), 0.0f);
}

TEST_F(PowerMeterTest, LongGapClearsEverything){
    uint32_t t = FeedConstant(50.0f, 1000, 2000);
    FeedConstant(5.0f, t + 60000, t + 60100);
    EXPECT_NEAR(PowerMeterPEP_W(t + 60100), 5.0f, 1e-3f);
}

TEST_F(PowerMeterTest, LateSampleGoesIntoTheCurrentBin){
    // A bridge reading stamped just before the bin a digital block opened
    uint32_t t = FeedConstant(50.0f, 1000, 2000);
    PowerMeterAddBridgeSample(50.0f, 0.0f, t - 3);
    EXPECT_NEAR(PowerMeterPEP_W(t), 50.0f, 1e-3f);
    EXPECT_NEAR(PowerMeterAvg_W(t), 50.0f, 1e-3f);
}

TEST_F(PowerMeterTest, CorrectionFollowsTheBridge){
    // The bridge reads 1 dB below the digital average for 30 s of transmit
    const float32_t r = powf(10.0f, -0.1f);
    uint32_t t = FeedWithBridge(20.0f, r, 0.0f, 1000, 31000);
    EXPECT_NEAR(PowerMeterCorrection_dB(), -1.0f, 0.05f);
    EXPECT_NEAR(PowerMeterAvg_W(t), 20.0f * r, 0.2f);
    EXPECT_NEAR(PowerMeterPEP_W(t), 20.0f * r, 0.2f);
    EXPECT_NEAR(PowerMeterBridgeAvg_W(t), 20.0f * r, 1e-3f);
}

TEST_F(PowerMeterTest, CorrectionWaitsForAFullyKeyedWindow){
    // Nothing until a whole 1 s window holds the carrier
    FeedWithBridge(20.0f, powf(10.0f, -0.2f), 0.0f, 1000, 1950);
    EXPECT_FLOAT_EQ(PowerMeterCorrection_dB(), 0.0f);
}

TEST_F(PowerMeterTest, CorrectionFollowsASteadyCarrierWithinSeconds){
    // One time constant after the window fills: about 63 % of the way to -2 dB
    FeedWithBridge(20.0f, powf(10.0f, -0.2f), 0.0f, 1000, 3000);
    float32_t k = PowerMeterCorrection_dB();
    EXPECT_LT(k, -1.0f);
    EXPECT_GT(k, -1.6f);
}

TEST_F(PowerMeterTest, CorrectionIgnoresModulatedSignals){
    // Two-tone-like blocks (PEP twice the average) with the bridge 1 dB low, which
    // is how the bridge reads a fast envelope: k must not learn from it
    uint32_t t100 = 100000;
    uint32_t nextBridge = 1000;
    while (t100 / 100 < 31000){
        uint32_t t = t100 / 100;
        PowerMeterAddDigitalBlock(40.0f, 20.0f * BLOCK, BLOCK, t);
        while (nextBridge <= t){
            PowerMeterAddBridgeSample(20.0f * powf(10.0f, -0.1f), 0.0f, nextBridge);
            nextBridge += 16;
        }
        t100 += BLOCK_MS_X100;
    }
    EXPECT_FLOAT_EQ(PowerMeterCorrection_dB(), 0.0f);
    EXPECT_NEAR(PowerMeterPEP_W(31000), 40.0f, 1e-3f);
    EXPECT_NEAR(PowerMeterAvg_W(31000), 20.0f, 1e-3f);
}

TEST_F(PowerMeterTest, CorrectionIsClamped){
    FeedWithBridge(20.0f, powf(10.0f, -0.8f), 0.0f, 1000, 61000);   // bridge 8 dB low
    EXPECT_NEAR(PowerMeterCorrection_dB(), -PM_K_LIMIT_DB, 1e-4f);
}

TEST_F(PowerMeterTest, CorrectionNeedsPower){
    FeedWithBridge(0.5f * PM_K_MIN_AVG_W, 0.5f, 0.0f, 1000, 31000);
    EXPECT_FLOAT_EQ(PowerMeterCorrection_dB(), 0.0f);
}

TEST_F(PowerMeterTest, CorrectionNeedsBridgeReadings){
    FeedConstant(20.0f, 1000, 31000);   // no AD7991
    EXPECT_FLOAT_EQ(PowerMeterCorrection_dB(), 0.0f);
    EXPECT_NEAR(PowerMeterAvg_W(31000), 20.0f, 1e-3f);
}

TEST_F(PowerMeterTest, CorrectionIsKeptPerBandAndPA){
    FeedWithBridge(20.0f, powf(10.0f, -0.1f), 0.0f, 1000, 31000);
    PowerMeterSetContext(5, true);                 // 100 W PA on the same band
    EXPECT_FLOAT_EQ(PowerMeterCorrection_dB(), 0.0f);
    PowerMeterSetContext(6, false);
    EXPECT_FLOAT_EQ(PowerMeterCorrection_dB(), 0.0f);
    PowerMeterSetContext(5, false);
    EXPECT_NEAR(PowerMeterCorrection_dB(), -1.0f, 0.05f);
}

TEST_F(PowerMeterTest, ContextChangeClearsTheReadings){
    uint32_t t = FeedConstant(50.0f, 1000, 2000);
    PowerMeterSetContext(7, false);
    EXPECT_FALSE(PowerMeterHasDigital(t));
    EXPECT_FLOAT_EQ(PowerMeterPEP_W(t), 0.0f);
}

TEST_F(PowerMeterTest, SWRFromLinearAverages){
    // |Gamma| = 1/3: SWR 2
    uint32_t t = FeedWithBridge(20.0f, 1.0f, 1.0f / 9.0f, 1000, 3000);
    EXPECT_NEAR(PowerMeterSWR(t), 2.0f, 0.01f);
}

TEST_F(PowerMeterTest, SWRHeldInSilence){
    uint32_t t = FeedWithBridge(20.0f, 1.0f, 1.0f / 9.0f, 1000, 3000);
    EXPECT_NEAR(PowerMeterSWR(t), 2.0f, 0.01f);
    // A pause: both detectors at their floors, where the ratio is meaningless
    for (uint32_t tb = t; tb < t + 2000; tb += 16)
        PowerMeterAddBridgeSample(1e-6f, 1e-6f, tb);
    EXPECT_NEAR(PowerMeterSWR(t + 2000), 2.0f, 0.01f);
}

TEST_F(PowerMeterTest, SWRIsFiniteWhenReflectedExceedsForward){
    uint32_t t = 1000;
    for (; t < 3000; t += 16) PowerMeterAddBridgeSample(5.0f, 6.0f, t);
    float32_t s = PowerMeterSWR(t);
    EXPECT_TRUE(std::isfinite(s));
    EXPECT_GT(s, 100.0f);
}
