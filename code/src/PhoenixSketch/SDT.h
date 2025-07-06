#ifndef SDT_H
#define SDT_H

#define TESTMODE

#ifndef TESTMODE
#include <Arduino.h>
#include <Adafruit_MCP23X17.h>
#include <arm_math.h>
#include <arm_const_structs.h>
#include <OpenAudio_ArduinoLibrary.h>  //https://github.com/chipaudette/OpenAudio_ArduinoLibrary
#include <utility/imxrt_hw.h>  // for setting I2S freq, Thanks, FrankB!
#define Debug(x) Serial.println(x)
#else
#include "../../test/arm_math.h"
#include "../../test/arm_const_structs.h"
#define Debug(x) serialprint(x)
#endif 

#define CLEAR_VAR(x) memset(x, 0, sizeof(x))

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Config.h"

#ifndef __STDC_LIB_EXT1__
typedef int errno_t;
#endif

#define BEGIN_TEENSY_SHUTDOWN 0
#define SHUTDOWN_COMPLETE 1

// The structure that holds the configuration information
#define EQUALIZER_CELL_COUNT                    14

#define BAND_80M                                0
#define BAND_60M                                1
#define BAND_40M                                2
#define BAND_30M                                3
#define BAND_20M                                4
#define BAND_17M                                5
#define BAND_15M                                6
#define BAND_12M                                7
#define BAND_10M                                8
#define BAND_6M                                 9

#define FIRST_BAND                              BAND_80M
#define LAST_BAND                               BAND_6M
#define NUMBER_OF_BANDS                         10
#define MAX_FAVORITES                           13

#define DECODER_STATE                           0  // 0 = off, 1 = on

#define SPECTRUM_ZOOM_MIN 0
#define SPECTRUM_ZOOM_1 0
#define SPECTRUM_ZOOM_2 1
#define SPECTRUM_ZOOM_4 2
#define SPECTRUM_ZOOM_8 3
#define SPECTRUM_ZOOM_16 4
#define SPECTRUM_ZOOM_MAX 4
#define SPECTRUM_RES 512
#define FFT_LENGTH SPECTRUM_RES

#define SAMPLE_RATE_MIN 6
#define SAMPLE_RATE_8K 0
#define SAMPLE_RATE_11K 1
#define SAMPLE_RATE_16K 2
#define SAMPLE_RATE_22K 3
#define SAMPLE_RATE_32K 4
#define SAMPLE_RATE_44K 5
#define SAMPLE_RATE_48K 6
#define SAMPLE_RATE_50K 7
#define SAMPLE_RATE_88K 8
#define SAMPLE_RATE_96K 9
#define SAMPLE_RATE_100K 10
#define SAMPLE_RATE_101K 11
#define SAMPLE_RATE_176K 12
#define SAMPLE_RATE_192K 13
#define SAMPLE_RATE_234K 14
#define SAMPLE_RATE_256K 15
#define SAMPLE_RATE_281K 16  // ??
#define SAMPLE_RATE_353K 17
#define SAMPLE_RATE_MAX 15

#ifndef PI
#define PI 3.1415926535897932384626433832795f
#endif
#ifndef HALF_PI
#define HALF_PI 1.5707963267948966192313216916398f
#endif
#ifndef TWO_PI
#define TWO_PI 6.283185307179586476925286766559f
#endif
#define FOUR_PI (2.0f * TWO_PI)
#define SIX_PI (3.0f * TWO_PI)
#define FIR_FILTER_WINDOW 1

enum KeyTypeId {
    KeyTypeId_Straight = 0,
    KeyTypeId_Keyer = 1
};

enum FilterType {
    Lowpass = 0,
    Highpass = 1,
    Bandpass = 2,
    Hilbert = 4,
    Notch = 5
};

enum TXRXType {
    TX = 0,
    RX = 1
};

enum AGCMode {
    AGCOff = 0,
    AGCLong = 1,
    AGCSlow = 2,
    AGCMed = 3,
    AGCFast = 5
};

enum ModulationType {
    USB = 0,
    LSB = 1,
    AM = 2,
    SAM = 3,
    IQ = 4,
    DCF77 = 29  // set the clock with the time signal station DCF77
};

enum NoiseReductionType {
  NROff = 0,
  NRKim = 1,
  NRSpectral = 2,
  NRLMS = 3
};

// This enum is used by an experimental Morse decoder.
enum MorseStates { state0,
              state1,
              state2,
              state3,
              state4,
              state5,
              state6 };

extern struct config_t {
    char versionSettings[10];                             // UNUSED
    AGCMode agc = AGCOff; // was AGCLong
    int32_t audioVolume = 30;                             // UNUSED
    float32_t rfGainAllBands_dB = 0;
    int32_t tuneIndex = DEFAULTFREQINCREMENT;             // UNUSED
    int64_t stepFineTune = FAST_TUNE_INCREMENT;          // UNUSED
    int32_t powerLevel = DEFAULT_POWER_LEVEL;             // UNUSED
    int32_t xmtMode = 0;                                  // UNUSED
    NoiseReductionType nrOptionSelect = NROff;
    uint8_t ANR_notchOn = 0;
    int32_t spectrumScale = 1; // was currentScale
    int32_t spectrum_zoom = 1;
    float32_t spectrum_display_scale = 20.0;              // UNUSED
    int32_t CWFilterIndex = 5;
    int32_t paddleDit = 36;                               // UNUSED
    int32_t paddleDah = 35;                               // UNUSED
    int32_t decoderFlag = DECODER_STATE; // CW decoder, 0=off, 1=on
    KeyTypeId keyType = KEYER_TYPE;                         // UNUSED
    int32_t currentWPM = DEFAULT_KEYER_WPM;
    float32_t sidetoneVolume = 20.0;                  // UNUSED
    int64_t cwTransmitDelay = 750;                       // UNUSED
    int32_t activeVFO = 0;                                // UNUSED
    int32_t freqIncrement = 5;                            // UNUSED
    float32_t freqCorrectionFactor = 0;                   // UNUSED
    int32_t currentBand = STARTUP_BAND;                   // UNUSED
    int32_t currentBandA = STARTUP_BAND;                  // UNUSED
    int32_t currentBandB = STARTUP_BAND;                  // UNUSED
    int64_t currentFreqA = CURRENT_FREQ_A;               // UNUSED
    int64_t currentFreqB = CURRENT_FREQ_B;               // UNUSED
    int32_t equalizerRec[EQUALIZER_CELL_COUNT] = { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 };
    int32_t equalizerXmt[EQUALIZER_CELL_COUNT] = { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 };
    int32_t currentMicThreshold = -10;                    // UNUSED
    float32_t currentMicCompRatio = 5.0;                  // UNUSED
    float32_t currentMicAttack = 0.1;                     // UNUSED
    float32_t currentMicRelease = 2.0;                    // UNUSED
    int32_t currentMicGain = -10;
    int32_t switchValues[18];                             // UNUSED
    float32_t LPFcoeff = 0.0;                             // UNUSED
    //float32_t NR_PSI = 0.0;                               // UNUSED
    //float32_t NR_alpha = 0.0;                             // UNUSED
    //float32_t NR_beta = 0.0;                              // UNUSED
    float32_t omegaN = 0.0;                               // UNUSED
    float32_t pll_fmax = 4000.0;                          // UNUSED
    float32_t powerOutCW[NUMBER_OF_BANDS];                // UNUSED
    float32_t powerOutSSB[NUMBER_OF_BANDS];               // UNUSED
    float32_t CWPowerCalibrationFactor[NUMBER_OF_BANDS];  // UNUSED
    float32_t SSBPowerCalibrationFactor[NUMBER_OF_BANDS]; // UNUSED
    float32_t IQAmpCorrectionFactor[NUMBER_OF_BANDS] = {1,1,1,1,1,1,1,1,1,1};
    float32_t IQPhaseCorrectionFactor[NUMBER_OF_BANDS] = {0,0,0,0,0,0,0,0,0,0};
    float32_t IQXAmpCorrectionFactor[NUMBER_OF_BANDS];    // UNUSED
    float32_t IQXPhaseCorrectionFactor[NUMBER_OF_BANDS];  // UNUSED
    float32_t IQXRecAmpCorrectionFactor[NUMBER_OF_BANDS]; // UNUSED
    float32_t IQXRecPhaseCorrectionFactor[NUMBER_OF_BANDS]; // UNUSED
    int32_t XAttenCW[NUMBER_OF_BANDS];                    // UNUSED
    int32_t XAttenSSB[NUMBER_OF_BANDS];                   // UNUSED
    int32_t RAtten[NUMBER_OF_BANDS];                      // UNUSED
    int64_t favoriteFreqs[MAX_FAVORITES];                // UNUSED
    int64_t lastFrequencies[NUMBER_OF_BANDS][2];         // UNUSED
    int32_t antennaSelection[NUMBER_OF_BANDS];            // UNUSED
    int64_t centerFreq = 7030000L;                       // UNUSED
    char mapFileName[50];                             // UNUSED
    char myCall[10];                                  // UNUSED
    char myTimeZone[10];                              // UNUSED
    int32_t separationCharacter = (int32_t)'.';               // UNUSED
    bool paddleFlip = PADDLE_FLIP;                     // UNUSED // false = right paddle = DAH, true = DIT
    int32_t sdCardPresent = 0;                            // UNUSED
    float32_t myLong = MY_LON;                            // UNUSED
    float32_t myLat = MY_LAT;                             // UNUSED
    int32_t currentNoiseFloor[NUMBER_OF_BANDS];           // UNUSED
    int32_t compressorFlag;                               // UNUSED
    int32_t receiveEQFlag = 0;                            // UNUSED
    int32_t xmitEQFlag = 0;                               // UNUSED
    int32_t CWToneIndex = 3;
    float32_t TransmitPowerLevelCW = 0.0;             // UNUSED
    float32_t TransmitPowerLevelSSB = 0.0;            // UNUSED
    float32_t SWR_PowerAdj[NUMBER_OF_BANDS];              // UNUSED
    float32_t SWRSlopeAdj[NUMBER_OF_BANDS] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // UNUSED
    int32_t SWR_R_Offset[NUMBER_OF_BANDS] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // UNUSED
} EEPROMData;

// Define a structure to hold the results of built-in-test routine
struct BIT {
  bool RF_I2C_present;
  bool RF_Si5351_present;
  bool BPF_I2C_present;
  bool V12_LPF_I2C_present;
  bool V12_LPF_AD7991_present;
  bool FRONT_PANEL_I2C_present;
  uint8_t AD7991_I2C_ADDR;
};

/** Contains the parameters that define a band */
struct band {
  int64_t freqVFO1_Hz; // Frequency of VFO1 in Hz (hardware mixer)
  float32_t freqVFO2_Hz;  // Frequency of VFO2 in Hz (DSP mixer)
  int64_t fBandLow_Hz;     // Lower band edge
  int64_t fBandHigh_Hz;    // Upper band edge
  const char *name;  // name of band
  ModulationType mode;
  int32_t FHiCut_Hz;
  int32_t FLoCut_Hz;
  float32_t RFgain_dB;       // dB
  uint8_t band_type;
  float32_t gainCorrection;  // is hardware dependent and has to be calibrated ONCE and hardcoded in the table below
  int32_t AGC_thresh;
  int16_t pixel_offset;
};

/** Contains the block of audio time samples being processed */
struct DataBlock {
    uint32_t N;             // Number of samples
    uint32_t sampleRate_Hz; // Sample rate
    float32_t *I;           // Contains N samples
    float32_t *Q;           // Contains N samples
}; 

typedef struct SR_Descriptor {
  const uint8_t SR_n;
  const uint32_t rate;
  const char *const text;
} SR_Desc;

/** Contains the structs and parameters for a decimation filter*/
struct DecimationFilter{
    float32_t M;  // decimation factor
    float32_t n_samplerate_Hz;  // samplerate before decimation, Hz
    float32_t n_att_dB;        // need here for later def's, dB
    float32_t n_desired_BW_Hz;  // desired max BW of the filters, Hz
    float32_t *FIR_dec_I_state;
    float32_t *FIR_dec_Q_state;
    float32_t *FIR_dec_coeffs;
    float32_t n_fpass;
    float32_t n_fstop;
    uint16_t n_dec_taps;
    arm_fir_decimate_instance_f32 FIR_dec_I;
    arm_fir_decimate_instance_f32 FIR_dec_Q;
};

extern float32_t (*EQ_Coeffs[14])[20];
extern float32_t CW_Filter_Coeffs2[64];
extern float32_t CW_AudioFilterCoeffs1[30];
extern float32_t CW_AudioFilterCoeffs2[30];
extern float32_t CW_AudioFilterCoeffs3[30];
extern float32_t CW_AudioFilterCoeffs4[30];
extern float32_t CW_AudioFilterCoeffs5[30];
/** Contains the filter structs and parameters */
struct FilterConfig {
    // Receive decimation filters
    struct DecimationFilter DecimateRxStage1;
    struct DecimationFilter DecimateRxStage2;
    const uint32_t DF1 = 4;  // decimation factor stage 1
    const uint32_t DF2 = 2;  // decimation factor stage 2
    uint32_t DF;               // combined decimation factor
    const float32_t n_att_dB = 90.0;        // need here for later def's, dB
    const float32_t n_desired_BW_Hz = 9000.0;  // desired max BW of the filters, kHz
    
    // Zoom FFT filters
    arm_biquad_casd_df1_inst_f32 biquadZoomI;
    arm_biquad_casd_df1_inst_f32 biquadZoomQ;
    uint8_t zoom_M;
    const uint32_t IIR_biquad_Zoom_FFT_N_stages = 4;

    // Convolution FIR filter
    const uint32_t m_NumTaps = (FFT_LENGTH / 2) + 1; // taps for the convolution FIR filter

    // Audio lowpass filter
    arm_biquad_casd_df1_inst_f32 biquadAudioLowPass;
    const uint32_t N_stages_biquad_lowpass1 = 1;
    float32_t biquad_lowpass1_coeffs[5] = { 0, 0, 0, 0, 0 };

    // Audio equalization filters
    const uint32_t eqNumStages = 4;
    arm_biquad_cascade_df2T_instance_f32 S_Rec[14];
    arm_biquad_cascade_df2T_instance_f32 S_Xmt[14];
    float32_t *eqFiltBuffer;
    float32_t *eqSumBuffer;

    // CW decode filter
    arm_fir_instance_f32 FIR_CW_Decode;
    float32_t FIR_CW_Decode_state[64 + 256 - 1];

    // CW audio bandpass filters
    float32_t CW_AudioFilter1_state[12] = {0};
    float32_t CW_AudioFilter2_state[12] = {0};
    float32_t CW_AudioFilter3_state[12] = {0};
    float32_t CW_AudioFilter4_state[12] = {0};
    float32_t CW_AudioFilter5_state[12] = {0};
    arm_biquad_cascade_df2T_instance_f32 S1_CW_AudioFilter1 = { 6, CW_AudioFilter1_state, CW_AudioFilterCoeffs1 };
    arm_biquad_cascade_df2T_instance_f32 S1_CW_AudioFilter2 = { 6, CW_AudioFilter2_state, CW_AudioFilterCoeffs2 };
    arm_biquad_cascade_df2T_instance_f32 S1_CW_AudioFilter3 = { 6, CW_AudioFilter3_state, CW_AudioFilterCoeffs3 };
    arm_biquad_cascade_df2T_instance_f32 S1_CW_AudioFilter4 = { 6, CW_AudioFilter4_state, CW_AudioFilterCoeffs4 };
    arm_biquad_cascade_df2T_instance_f32 S1_CW_AudioFilter5 = { 6, CW_AudioFilter5_state, CW_AudioFilterCoeffs5 };

    // Interpolation filters
    arm_fir_interpolate_instance_f32 FIR_int1;
    arm_fir_interpolate_instance_f32 FIR_int2;
    float32_t FIR_int1_coeffs[48];
    float32_t *FIR_int1_state;
    float32_t FIR_int2_coeffs[32];
    float32_t *FIR_int2_state;

    FilterConfig() {
        // Receive decimation filters
        DF = DF1 * DF2;        // decimation factor
        
        // Zoom FFT filters
        biquadZoomI.numStages = IIR_biquad_Zoom_FFT_N_stages;
        biquadZoomQ.numStages = IIR_biquad_Zoom_FFT_N_stages;
        biquadZoomI.pState = (float32_t *)malloc(sizeof(float32_t) * (IIR_biquad_Zoom_FFT_N_stages * 4));
        biquadZoomQ.pState = (float32_t *)malloc(sizeof(float32_t) * (IIR_biquad_Zoom_FFT_N_stages * 4));
        
        // Audio lowpass filters
        biquadAudioLowPass.numStages = N_stages_biquad_lowpass1;
        biquadAudioLowPass.pState = (float32_t *)malloc(sizeof(float32_t) * (N_stages_biquad_lowpass1 * 4));
        biquadAudioLowPass.pCoeffs = biquad_lowpass1_coeffs;

        // Audio equalization filters
        eqFiltBuffer = (float32_t *)malloc(sizeof(float32_t) * (READ_BUFFER_SIZE/DF));
        eqSumBuffer = (float32_t *)malloc(sizeof(float32_t) * (READ_BUFFER_SIZE/DF));
        for (size_t i = 0; i<14; i++){
            S_Rec[i].numStages = eqNumStages;
            S_Rec[i].pState = (float32_t *)malloc(sizeof(float32_t) * eqNumStages * 2);
            S_Rec[i].pCoeffs = *EQ_Coeffs[i];

            S_Xmt[i].numStages = eqNumStages;
            S_Xmt[i].pState = (float32_t *)malloc(sizeof(float32_t) * eqNumStages * 2);
            S_Xmt[i].pCoeffs = *EQ_Coeffs[i];
        }

        // CW decode filter
        arm_fir_init_f32(&FIR_CW_Decode, 64, CW_Filter_Coeffs2, FIR_CW_Decode_state, 256);

        // Interpolation filters
        // First stage undoes the decimation factor DF2 (2).
        // Block size is READ_BUFFER_SIZE / (DF1*DF2) = 2048 / 8 = 256
        // Number of taps is 48
        // So state array size is: Num Taps + Block Size - 1
        uint16_t INT1_STATE_SIZE = 48 +  READ_BUFFER_SIZE / (DF1*DF2)  - 1;        
        FIR_int1_state = (float32_t *)malloc(sizeof(float32_t) * INT1_STATE_SIZE);
        arm_fir_interpolate_init_f32(&FIR_int1, DF2, 48, FIR_int1_coeffs, FIR_int1_state, READ_BUFFER_SIZE/DF);

        // Second stage undoes DF1 (4)
        // operates on a block size of READ_BUFFER_SIZE / (DF1*DF2) * DF2 = 2048/4 = 512
        // Number of taps is 32
        // So state array size is:
        uint16_t INT2_STATE_SIZE = 32 + READ_BUFFER_SIZE / DF1 - 1;
        FIR_int2_state = (float32_t *)malloc(sizeof(float32_t) * INT2_STATE_SIZE);
        arm_fir_interpolate_init_f32(&FIR_int2, DF1, 32, FIR_int2_coeffs, FIR_int2_state, READ_BUFFER_SIZE/DF1);
    }
    ~FilterConfig() {
        free(biquadZoomI.pState);
        free(biquadZoomQ.pState);
        free(biquadAudioLowPass.pState);
        free(eqFiltBuffer);
        free(eqSumBuffer);
        for (size_t i = 0; i<14; i++){
            free(S_Rec[i].pState);
            free(S_Xmt[i].pState);
        }
        free(FIR_int1_state);
        free(FIR_int2_state);
    }
};

struct AGCConfig {
    // Start variables taken from wdsp
    const float32_t tau_attack            = 0.001; // tau_attack
    float32_t tau_decay             = 0.250;
    const float32_t n_tau                 = 4; 
    const float32_t fixed_gain            = 20.0;
    const float32_t max_input             = 1.0;
    const float32_t out_targ              = 1.0; // target value of audio after AGC
    const float32_t var_gain              = 1.5;
    const float32_t tau_fast_backaverage  = 0.250;      // tau_fast_backaverage
    float32_t fast_backaverage            = 0.0;
    float32_t hang_backaverage            = 0.0;
    const float32_t tau_fast_decay        = 0.005;      // tau_fast_decay
    const float32_t pop_ratio             = 5.0;        // pop_ratio
    const float32_t hang_enable           = 1; 
    const float32_t tau_hang_backmult     = 0.500;      // tau_hang_backmult
    float32_t hangtime              = 0.250;      // hangtime
    float32_t hang_thresh           = 0.250;      // hang_thresh
    const float32_t tau_hang_decay        = 0.100;      // tau_hang_decay
    float32_t max_gain                    = 10000.0;
    float32_t ring_max = 0.0;
    const uint8_t pmode = 1;
    uint8_t state = 0;
    uint8_t agc_action = 0;
    uint8_t decay_type = 0;
    float32_t volts = 0.0;
    float32_t save_volts = 0.0;
    // #define MAX_SAMPLE_RATE (24000.0) 
    // #define MAX_N_TAU (8)
    // #define MAX_TAU_ATTACK (0.01)
    uint32_t ring_buffsize = (uint32_t)(24000.0 * 8 * 0.01 + 1);
    float32_t hang_level;
    float32_t hang_backmult;
    float32_t onemhang_backmult;
    float32_t hang_decay_mult;
    uint32_t attack_buffsize;
    uint32_t in_index;
    uint32_t out_index;
    float32_t attack_mult;
    float32_t decay_mult;
    float32_t fast_decay_mult;
    float32_t fast_backmult;
    float32_t onemfast_backmult;
    float32_t out_target;
    float32_t inv_out_target;
    float32_t inv_max_input;
    float32_t min_volts;
    float32_t slope_constant;
    int32_t hang_counter;

    float32_t *ring;
    float32_t *abs_ring;

    AGCConfig(){
        ring = (float32_t *)malloc(sizeof(float32_t) * ring_buffsize * 2);
        abs_ring = (float32_t *)malloc(sizeof(float32_t) * ring_buffsize);
    }

    ~AGCConfig(){
        free(ring);
        free(abs_ring);
    }
};

///////////////////////////////////////////////////
// This enables some testing functions. Comment out for prod.
#ifdef TESTMODE
//typedef float float32_t;
#define DMAMEM
#define FASTRUN
#endif
//////////////////////////////////////////////////

#include "RFBoard.h"

#define ESUCCESS             0
#define ENOI2C              -1
#define EGPIOWRITEFAIL      -2
#define EFAIL               -10


// Functions for the Mode state machine
#include "Mode.h"
// Functions for the UI state machine
#include "UI.h"

#include "ModeSm.h"
#include "UISm.h"

// Others
#include "Loop.h"
#include "MainBoard_Display.h"
#include "DSP_FFT.h"
#include "DSP_Noise.h"
#include "DSP_CWProcessing.h"
#include "DSP.h"
#include "MainBoard_AudioIO.h"
#include "FrontPanel.h"
#include "FrontPanel_Rotary.h"
#ifdef TESTMODE
#include "../../test/SignalProcessing_mock.h" // remove from prod
#endif

extern struct BIT bit_results;
extern struct band bands[];
extern const struct SR_Descriptor SR[];
extern FilterConfig filters;
extern AGCConfig agc;
extern UISm uiSM;
extern ModeSm modeSM;
extern bool displayFFTUpdated; /** Set true when psdnew is updated */
extern bool CWLocked;
extern float32_t psdnew[]; /** Holds the current PSD data for the power spectrum display */
extern float32_t psdold[]; /** Holds the prior PSD data for the power spectrum display */
extern AudioRecordQueue Q_in_L;
extern AudioRecordQueue Q_in_R;
extern AudioPlayQueue Q_out_L;
extern AudioPlayQueue Q_out_R;

extern const float32_t CWToneOffsetsHz[];
extern uint8_t SampleRate;
extern float32_t SAM_carrier_freq_offset;
extern float32_t SAM_carrier_freq_offsetOld;

extern char morseCharacter;
extern bool morseCharacterUpdated;
extern uint32_t audioVolume;

#endif // SDT_H