#ifndef SDT_H
#define SDT_H

#define RIGNAME "T41-EP SDT"
#define VERSION "Phx 1.0"

#define Debug(x) Serial.println(x)

#include <Arduino.h>
#include <Adafruit_MCP23X17.h>
#include <OpenAudio_ArduinoLibrary.h>  //https://github.com/chipaudette/OpenAudio_ArduinoLibrary
#include <utility/imxrt_hw.h>
#include <arm_math.h>
#include <arm_const_structs.h>
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

#define CLEAR_VAR(x) memset(x, 0, sizeof(x))

#define BEGIN_TEENSY_SHUTDOWN 0
#define SHUTDOWN_COMPLETE 1

// The structure that holds the configuration information
#define EQUALIZER_CELL_COUNT                    14

#define BAND_160M                               0
#define BAND_80M                                1
#define BAND_60M                                2
#define BAND_40M                                3
#define BAND_30M                                4
#define BAND_20M                                5
#define BAND_17M                                6
#define BAND_15M                                7
#define BAND_12M                                8
#define BAND_10M                                9
#define BAND_6M                                 10
#define BAND_4M                                 11

#define FIRST_BAND                              BAND_160M
#define LAST_BAND                               BAND_6M
#define NUMBER_OF_BANDS                         12
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

#define VFO_A 0
#define VFO_B 1

enum KeyTypeId {
    KeyTypeId_Straight = 0,
    KeyTypeId_Keyer = 1,
    KeyTypeId_Invalid = 8
};

enum FilterType {
    Lowpass = 0,
    Highpass = 1,
    Bandpass = 2,
    Hilbert = 4,
    Notch = 5
};

enum TXRXType {
    TX = 1,
    RX = 0
};

enum AGCMode {
    AGCOff = 0,
    AGCLong = 1,
    AGCSlow = 2,
    AGCMed = 3,
    AGCFast = 5,
    AGCInvalid = 8
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
    NRLMS = 3,
    NRInvalid = 8
};

enum VolumeFunction {
    AudioVolume = 0,
    AGCGain = 1,
    MicGain = 2,
    SidetoneVolume = 3,
    InvalidVolumeFunction = 100
    //NoiseFloorLevel = 4
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
    AGCMode agc = AGCOff; /** AGC mode */
    int32_t audioVolume = 30; /** Output audio amplitude */
    float32_t rfGainAllBands_dB = 0; /** Gain applied to the IQ samples in DSP chain */
    int64_t stepFineTune = FAST_TUNE_INCREMENT; /** Increment value for fine tune */
    NoiseReductionType nrOptionSelect = NROff; /** Noise reduction mode */
    uint8_t ANR_notchOn = 0; /** Automatic notch filter on/off */
    int32_t spectrumScale = 1; /** dB/pixel selection for spectrum display */
    int16_t spectrumNoiseFloor[NUMBER_OF_BANDS] = {50,50,50,50,50,50,50,50,50,50,50,50}; /** Shift spectrum up/down on display */
    uint32_t spectrum_zoom = 1; /** Zoom level for spectrum */
    int32_t CWFilterIndex = 5; /** Selects the receive CW audio filter */
    int32_t CWToneIndex = 3; /** Selects the transmitted CW tone frequency */
    int32_t decoderFlag = DECODER_STATE; /** CW decoder on/off */
    KeyTypeId keyType = KEYER_TYPE; /** CW key type: straight or keyer */
    int32_t currentWPM = DEFAULT_KEYER_WPM; /** CW words per minute for keyer + decoder */
    float32_t sidetoneVolume = 20.0; /** CW transmit sidetone volume */
    int32_t freqIncrement = 1000; /** Increment value for center tune */
    float32_t freqCorrectionFactor = 0; /** Correction value for Si5351 VFO */
    uint8_t activeVFO = 0; /** Which VFO is currently active (0 or 1) */
    ModulationType modulation[2] = {LSB, LSB}; /** Modulation type for each VFO */
    int32_t currentBand[2] = {STARTUP_BAND, STARTUP_BAND}; /** Band for each VFO */
    int64_t centerFreq_Hz[2] = {7030000L,7030000L}; /** VFO center frequency for each VFO */
    int64_t fineTuneFreq_Hz[2] = {0, 0}; /** Fine tune frequency for each VFO */
    int32_t equalizerRec[EQUALIZER_CELL_COUNT] = { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 }; /** Receive audio equalizer amplitudes */
    int32_t equalizerXmt[EQUALIZER_CELL_COUNT] = { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 }; /** Transmit audio equalizer amplitudes */
    int32_t currentMicGain = -10; /** Gain of the mic used for SSB */
    float32_t dbm_calibration = 17.5; /** Calibrates the S-meter scale on the display */
    float32_t powerOutCW[NUMBER_OF_BANDS] = {5,5,5,5,5,5,5,5,5,5,5,5}; /** Set output power in Watts in CW mode */
    float32_t powerOutSSB[NUMBER_OF_BANDS] = {5,5,5,5,5,5,5,5,5,5,5,5}; /** Set output power in Watts in SSB mode */
    float32_t IQAmpCorrectionFactor[NUMBER_OF_BANDS] =   {1,1,1,1,1,1,1,1,1,1,1,1}; /** Receive IQ calibration amplitude correction */
    float32_t IQPhaseCorrectionFactor[NUMBER_OF_BANDS] = {0,0,0,0,0,0,0,0,0,0,0,0}; /** Receive IQ calibration phase correction */
    float32_t XAttenCW[NUMBER_OF_BANDS] = {0,0,0,0,0,0,0,0,0,0,0,0}; /** RF board transmit attenuation in CW mode */
    float32_t XAttenSSB[NUMBER_OF_BANDS] = {0,0,0,0,0,0,0,0,0,0,0,0}; /** RF board transmit attenuation in SSB mode */
    float32_t RAtten[NUMBER_OF_BANDS] = {0,0,0,0,0,0,0,0,0,0,0,0}; /** RF board receive attenuation */
    int64_t lastFrequencies[NUMBER_OF_BANDS][3] = {{1850000,0,1},{3700000,0,1},{5351500,0,0},
                    {7150000,0,1},{10125000,0,0},{14200000,0,0},{18100000,0,0},
                    {21200000,0,0},{24920000,0,0},{28350000,0,0},{50100000,0,0},{70300000,0,0}}; /** center tune, fine tune, modulation */
    int32_t antennaSelection[NUMBER_OF_BANDS] = {0,0,0,0,0,0,0,0,0,0,0,0}; /** Antenna selection per band */
    bool keyerFlip = KEYER_FLIP; /**false = right paddle = DAH, true = DIT */
    float32_t SWR_F_SlopeAdj[NUMBER_OF_BANDS] = {0,0,0,0,0,0,0,0,0,0,0,0}; /** SWR calibration */
    float32_t SWR_R_SlopeAdj[NUMBER_OF_BANDS] = {0,0,0,0,0,0,0,0,0,0,0,0}; /** SWR calibration */
    float32_t SWR_R_Offset[NUMBER_OF_BANDS] = {0,0,0,0,0,0,0,0,0,0,0,0}; /** SWR calibration */
    float32_t SWR_F_Offset[NUMBER_OF_BANDS] = {0,0,0,0,0,0,0,0,0,0,0,0}; /** SWR calibration */
} ED;

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
    float32_t freqVFO2_Hz;  // UNUSED Frequency of VFO2 in Hz (DSP mixer) 
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
        biquadZoomI.pState = (float32_t *)calloc(IIR_biquad_Zoom_FFT_N_stages * 4, sizeof(float32_t));
        biquadZoomQ.pState = (float32_t *)calloc(IIR_biquad_Zoom_FFT_N_stages * 4, sizeof(float32_t));
        
        // Audio lowpass filters
        biquadAudioLowPass.numStages = N_stages_biquad_lowpass1;
        biquadAudioLowPass.pState = (float32_t *)calloc(N_stages_biquad_lowpass1 * 4, sizeof(float32_t));
        biquadAudioLowPass.pCoeffs = biquad_lowpass1_coeffs;

        // Audio equalization filters
        eqFiltBuffer = (float32_t *)calloc(READ_BUFFER_SIZE/DF, sizeof(float32_t));
        eqSumBuffer = (float32_t *)calloc(READ_BUFFER_SIZE/DF, sizeof(float32_t));
        for (size_t i = 0; i<14; i++){
            S_Rec[i].numStages = eqNumStages;
            S_Rec[i].pState = (float32_t *)calloc(eqNumStages * 2, sizeof(float32_t));
            S_Rec[i].pCoeffs = NULL;  // Will be set in InitializeFilters()

            S_Xmt[i].numStages = eqNumStages;
            S_Xmt[i].pState = (float32_t *)calloc(eqNumStages * 2, sizeof(float32_t));
            S_Xmt[i].pCoeffs = NULL;  // Will be set in InitializeFilters()
        }

        // CW decode filter
        arm_fir_init_f32(&FIR_CW_Decode, 64, CW_Filter_Coeffs2, FIR_CW_Decode_state, 256);

        // Interpolation filters
        // First stage undoes the decimation factor DF2 (2).
        // Block size is READ_BUFFER_SIZE / (DF1*DF2) = 2048 / 8 = 256
        // Number of taps is 48
        // So state array size is: Num Taps + Block Size - 1
        uint16_t INT1_STATE_SIZE = 48 +  READ_BUFFER_SIZE / (DF1*DF2)  - 1;
        FIR_int1_state = (float32_t *)calloc(INT1_STATE_SIZE, sizeof(float32_t));
        arm_fir_interpolate_init_f32(&FIR_int1, DF2, 48, FIR_int1_coeffs, FIR_int1_state, READ_BUFFER_SIZE/DF);

        // Second stage undoes DF1 (4)
        // operates on a block size of READ_BUFFER_SIZE / (DF1*DF2) * DF2 = 2048/4 = 512
        // Number of taps is 32
        // So state array size is:
        uint16_t INT2_STATE_SIZE = 32 + READ_BUFFER_SIZE / DF1 - 1;
        FIR_int2_state = (float32_t *)calloc(INT2_STATE_SIZE, sizeof(float32_t));
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
    float32_t tau_decay                   = 0.250;
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
    float32_t hangtime                    = 0.250;      // hangtime
    float32_t hang_thresh                 = 0.250;      // hang_thresh
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
#include "HardwareSM.h"

// Others
#include "Loop.h"
#include "Tune.h"
#include "MainBoard_Display.h"
#include "DSP_FFT.h"
#include "DSP_Noise.h"
#include "DSP_CWProcessing.h"
#include "DSP.h"
#include "MainBoard_AudioIO.h"
#include "FrontPanel.h"
#include "FrontPanel_Rotary.h"
#include "RFBoard.h"
#include "LPFBoard.h"
#include "BPFBoard.h"
#include "CAT.h"
#include "Storage.h"

extern struct BIT bit_results;
extern struct band bands[];
extern const struct SR_Descriptor SR[];
extern FilterConfig filters;
extern AGCConfig agc;
extern UISm uiSM;
extern ModeSm modeSM;
extern bool displayFFTUpdated; /** Set true when psdnew is updated */
extern bool psdupdated;
extern float32_t psdnew[]; /** Holds the current PSD data for the power spectrum display */
extern float32_t psdold[]; /** Holds the prior PSD data for the power spectrum display */
extern float32_t audioYPixel[];
extern AudioRecordQueue Q_in_L;
extern AudioRecordQueue Q_in_R;
extern AudioPlayQueue Q_out_L;
extern AudioPlayQueue Q_out_R;

extern const float32_t CWToneOffsetsHz[];
extern uint8_t SampleRate;
extern float32_t SAM_carrier_freq_offset;
extern float32_t SAM_carrier_freq_offsetOld;

extern VolumeFunction volumeFunction;

extern int64_t elapsed_micros_idx_t;
extern int64_t elapsed_micros_sum;
extern float32_t elapsed_micros_mean;
extern elapsedMicros usec;

// Hardware definitions
#define RXTX        22 // Transmit/Receive (H=TX,L=RX)
#define CW_ON_OFF   33 // CW on / off (H=ON,L=OFF) (V12 hardware)
#define XMIT_MODE   34 // Transmit mode (H=SSB,L=CW) (V12 hardware)
#define KEY1        36 // Tip for Straight key
#define KEY2        35 // Ring
#define PTT         37
#define FOR         26
#define REV         27
#define CAL         38  // RX board calibration control (H=CAL,L=normal)

// The 32 bit register that records the state of the radio hardware
extern uint32_t hardwareRegister;

// The bit map for hardwareRegister
#define LPFBAND0BIT  0
#define LPFBAND1BIT  1
#define LPFBAND2BIT  2
#define LPFBAND3BIT  3
#define ANT0BIT      4
#define ANT1BIT      5
#define XVTRBIT      6
#define PA100WBIT    7
#define TXBPFBIT     8
#define RXBPFBIT     9
#define RXTXBIT      10
#define CWBIT        11
#define MODEBIT      12
#define CALBIT       13
#define CWVFOBIT     14
#define SSBVFOBIT    15
#define TXATTLSB     16
#define TXATTMSB     21
#define RXATTLSB     22
#define RXATTMSB     27
#define BPFBAND0BIT  28
#define BPFBAND1BIT  29
#define BPFBAND2BIT  30
#define BPFBAND3BIT  31

#define BAND_NF_BCD   0b1111
#define BAND_6M_BCD   0b1010
#define BAND_10M_BCD  0b1001
#define BAND_12M_BCD  0b1000
#define BAND_15M_BCD  0b0111
#define BAND_17M_BCD  0b0110
#define BAND_20M_BCD  0b0101
#define BAND_30M_BCD  0b0100
#define BAND_40M_BCD  0b0011
#define BAND_60M_BCD  0b0000
#define BAND_80M_BCD  0b0010
#define BAND_160M_BCD 0b0001

#define GET_BIT(byte, bit) (((byte) >> (bit)) & 1)
#define SET_BIT(byte, bit) ((byte) |= (1 << (bit)));buffer_add()
#define CLEAR_BIT(byte, bit) ((byte) &= ~(1 << (bit)));buffer_add()
#define TOGGLE_BIT(byte, bit) ((byte) ^= (1 << (bit)));buffer_add()
#define GET_LPF_BAND (uint8_t)(hardwareRegister & 0x0000000F)

// Every time the value of hardwareRegister is updated, store this in a rolling buffer
#define REGISTER_BUFFER_SIZE 100

typedef struct {
    uint32_t timestamp;
    uint32_t register_value;
} BufferEntry;

typedef struct {
    BufferEntry entries[REGISTER_BUFFER_SIZE];
    size_t head;        // Index where next entry will be written
    size_t count;       // Number of valid entries (up to BUFFER_SIZE)
} RollingBuffer;
extern RollingBuffer buffer;
void buffer_add(void);
void buffer_flush(void);
void buffer_pretty_print(void);
void buffer_pretty_buffer_array(void);
void pretty_print_line(BufferEntry entry);
void buffer_pretty_print_last_entry(void);
void initTempMon(uint16_t freq, uint32_t lowAlarmTemp, uint32_t highAlarmTemp, uint32_t panicAlarmTemp);
float32_t TGetTemp(void);
void Flag(uint8_t val);

void MyDelay(unsigned long millisWait);
void UpdateDitLength(void);

#endif // SDT_H