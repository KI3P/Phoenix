/**
 * @file SimBench.h
 * @brief The simulated bench: the test signals and instruments that the radio
 *        simulator and its screenshot tools put around the firmware
 *
 * - SimRouteTestSignals(): the signals each calibration needs
 *   - Frequency: a frequency standard on the dial, offset by the simulated
 *     reference crystal's error less the correction the firmware programs
 *   - Receive IQ: the RX IQ test tone, with the mock's 10 % amplitude error
 *   - Transmit IQ (dual VFO): the transmitter looped back into the receiver
 *   and, on leaving each, the input source that was selected before it.
 * - SimBridgeADC(): the forward and reflected power bridge (the AD7991's
 *   readings), as radio 2 measured on 2026-11-02, with a 50 Ohm load, an open
 *   or a short on the antenna connector (simConnector).
 * - SimWattmeter_W(): the true power into the connector, as a wattmeter shows it.
 *
 * Call SimRouteTestSignals() once per loop() pass. Switch the bridge on with
 * Adafruit_I2CDevice::setMockADCProvider(SimBridgeADC).
 *
 * Include this in one translation unit only (its functions are static).
 */

#ifndef SIMBENCH_H
#define SIMBENCH_H

#include <cmath>
#include <iostream>

#include "SDT.h"
#include "MainBoard_AudioIO.h"
#include "HardwareSm.h"
#include "UISm.h"
#include "OpenAudio_ArduinoLibrary.h"
#include "Adafruit_I2CDevice.h"

extern UISm uiSM;
int32_t Si5351_Mock_GetCorrection(void);

///////////////////////////////////////////////////////////////////////////////
// Test signals
///////////////////////////////////////////////////////////////////////////////

/** The simulated reference crystal's error, in ppb. Frequency calibration
 *  nulls it when the correction factor equals it. */
static int32_t simCrystalError_ppb = 0;

static bool simRouted = false;                 // a calibration owns the input
static AudioInputSource simSavedSource = AUDIO_SOURCE_SINGLE_TONE;
static RFHardwareState simPreviousRFState = RFInvalid;
static int32_t simPreviousBand = -1;
static bool simInFrequencyCal = false;
static int64_t simStandard_Hz = 0;             // the frequency standard's frequency
static double simShownOffset_Hz = 0;

/** Switch the input to source for a calibration, remembering the operator's */
static void SimTakeInput(AudioInputSource source){
    if (!simRouted){
        simSavedSource = getAudioInputSource();
        simRouted = true;
    }
    setAudioInputSource(source);
}

/** Give the operator's input source back after a calibration */
static void SimReleaseInput(void){
    if (!simRouted) return;
    simRouted = false;
    setAudioInputSource(simSavedSource);
    std::cout << "Audio Source (auto): back to " << getAudioInputSourceName() << std::endl;
}

/**
 * The frequency standard, while the frequency calibration screen is up.
 * The standard sits where the dial was on entry. The receive LO, at the centre
 * frequency, is off by the crystal's error less the programmed correction, which
 * moves the standard in the receiver's passband just as on the bench.
 */
static void SimFrequencyStandard(void){
    bool inCal = (uiSM.state_id == UISm_StateId_CALIBRATE_FREQUENCY);
    if (inCal && !simInFrequencyCal){
        simStandard_Hz = GetTXRXFreq(ED.activeVFO);
        SimTakeInput(AUDIO_SOURCE_SINGLE_TONE);
        std::cout << "Frequency standard at " << simStandard_Hz << " Hz (crystal error "
                  << simCrystalError_ppb << " ppb)" << std::endl;
    } else if (!inCal && simInFrequencyCal){
        setSingleToneOffsetHz(1000.0);
        SimReleaseInput();
    }
    simInFrequencyCal = inCal;
    if (!inCal) return;

    // The single tone at Fs/4 + offset is the carrier at centre - Fs/4 - offset
    double lo_Hz = (double)ED.centerFreq_Hz[ED.activeVFO];
    double loError = (double)(simCrystalError_ppb - Si5351_Mock_GetCorrection()) * 1e-9;
    double offset = lo_Hz * (1.0 + loError) - SR[SampleRate].rate / 4.0 - (double)simStandard_Hz;
    if (offset != simShownOffset_Hz){
        setSingleToneOffsetHz(offset);
        simShownOffset_Hz = offset;
    }
}

/** Route the test signals each calibration needs. Call once per loop() pass. */
static void SimRouteTestSignals(void){
    SimFrequencyStandard();

    RFHardwareState state = GetRFHardwareState();
    int32_t band = ED.currentBand[ED.activeVFO];
    if ((state == simPreviousRFState) && (band == simPreviousBand)) return;
    simPreviousRFState = state;
    simPreviousBand = band;
    switch (state){
        case RFCalReceiveIQ:
            Q_in_L_Ex.setOscillatorSource(nullptr);
            Q_in_R_Ex.setOscillatorSource(nullptr);
            // The test tone with the mock's 10 % amplitude error, in the bin
            // the calibration reads as the wanted signal
            SimTakeInput(AUDIO_SOURCE_RXIQ_USB);
            std::cout << "Audio Source (auto): RX IQ tone (USB) for Receive IQ calibration" << std::endl;
            break;
        case RFCalTransmitIQ:
            // The calibration oscillator feeds the transmit IQ modulator, and
            // the transmitter loops back into the receiver
            Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);
            Q_in_R_Ex.setOscillatorSource(&transmitIQcal_oscillator);
            SimTakeInput(AUDIO_SOURCE_FEEDBACK);
            std::cout << "Audio Source (auto): feedback for Transmit IQ calibration" << std::endl;
            break;
        case RFCalTransmitIQSingleVFO:
            // Real transmit into the RF board's output: the receiver sees nothing
            // of it, and the operator reads the sidebands on an analyser
            Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);
            Q_in_R_Ex.setOscillatorSource(&transmitIQcal_oscillator);
            break;
        default:
            Q_in_L_Ex.setOscillatorSource(nullptr);
            Q_in_R_Ex.setOscillatorSource(nullptr);
            if (!simInFrequencyCal) SimReleaseInput();
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// The power bridge: radio 2, and the standard on the connector
//
// - the PA's output against the CW attenuation is radio 2's level 2 sweep
//   (code/tools/swr/pa_curve_radio2_level2_2026-11-02.csv);
// - the bridge lines, leaks, the power into the open and short, the SSB tone's
//   transfer and the 10 m oscillation into the short are the RADIO2 profile of
//   PowerAutoCalibration_E2E_test.cpp (code/tools/swr bench sheets).
///////////////////////////////////////////////////////////////////////////////

#define SIM_GETHWRBITS(LSB,len) ((hardwareRegister >> LSB) & ((1 << len) - 1))

enum SimConnector { CONNECT_LOAD, CONNECT_OPEN, CONNECT_SHORT };
static SimConnector simConnector = CONNECT_LOAD;
static uint32_t simConversions = 0;

/** Radio 2's turns on L44, the coupler's secondary */
static const uint8_t RADIO2_TURNS = 20;

static const char *SimConnectorName(SimConnector c){
    switch (c){
        case CONNECT_OPEN:  return "open";
        case CONNECT_SHORT: return "short";
        default:            return "50 Ohm load";
    }
}

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

/** The band whose LPF the hardware register selects, or -1 */
static int32_t SimHardwareBand(void){
    for (int32_t b = BAND_160M; b <= BAND_6M; b++)
        if (BandToBCD(b) == SIM_GETHWRBITS(LPFBAND0BIT, 4))
            return b;
    return -1;
}

/** True while the PA is driven, in CW or SSB */
static bool SimTransmitting(void){
    return GET_BIT(hardwareRegister, RXTXBIT) &&
           (GET_BIT(hardwareRegister, CWBIT) || GET_BIT(hardwareRegister, MODEBIT));
}

/** True forward power on the line, in dBm */
static float32_t SimForwardPower_dBm(void){
    int32_t b = SimHardwareBand();
    if (b < 0) return -30.0f;
    if (GET_BIT(hardwareRegister, CWBIT))
        return PACurve_dBm(b, SIM_GETHWRBITS(TXATTLSB, 6) / 2.0f);
    float32_t gain_dB;
    if (!GetSSBGainOverride(&gain_dB)){
        bool PAsel;
        gain_dB = ED.PowerCal_20W_DSP_Gain_correction_dB[b] + CalculateSSBTXGain(ED.powerOutSSB[b], &PAsel);
    }
    float32_t P = 10.0f * log10f(3000.0f) + AtBand(r2_transfer, b) * (gain_dB - SSBGainFor3W(b));
    if (simConnector == CONNECT_OPEN) P += AtBand(r2_open_dB, b);
    if (simConnector == CONNECT_SHORT) P += AtBand(r2_short_dB, b);
    return P;
}

/** The AD7991's answer: channel 0 forward, 1 reflected; counts = mV */
static uint16_t SimBridgeADC(uint8_t channel){
    if (!SimTransmitting())
        return channel == 0 ? 317 : 370;   // radio 2's key-up floor
    int32_t b = SimHardwareBand();
    if (b < 0) return 0;
    float32_t dither = 2.5f * sinf(0.7f * (float32_t)(simConversions++));
    // Radio 2's PA oscillates into a 10 m short, whatever the drive
    if ((b == BAND_10M) && (simConnector == CONNECT_SHORT))
        return (uint16_t)lroundf(1930.0f + dither);
    float32_t Pf = SimForwardPower_dBm();
    float32_t Pr = Pf;
    switch (simConnector){
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

/** The wattmeter's reading, in W to 0.01 W: the true forward power */
static float32_t SimWattmeter_W(void){
    float32_t W = powf(10.0f, SimForwardPower_dBm() / 10.0f) / 1000.0f;
    return roundf(W * 100.0f) / 100.0f;
}

#endif // SIMBENCH_H
