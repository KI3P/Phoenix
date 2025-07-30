#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"
#include "SignalProcessing_mock.h"
#include <sys/time.h>

float32_t get_max(float32_t *d, uint32_t Nsamples){
    float32_t max = -1e6;
    for (size_t i = 0; i<Nsamples; i++){
        if (d[i] > max)
            max = d[i];
    }
    return max;
}

int32_t get_max(int32_t *d, uint32_t Nsamples){
    int32_t max = -10000000;
    for (size_t i = 0; i<Nsamples; i++){
        if (d[i] > max)
            max = d[i];
    }
    return max;
}

int CreateIQToneWithPhase(int16_t *I, int16_t *Q, int Nsamples, int sampleRate_Hz,
                int tone_Hz, int phase_index,float amplitude){
    float oneoverfs = 1.0 / (float)sampleRate_Hz;
    for (int i=0;i<Nsamples;i++){
        I[i] = (int16_t)(32768.0*amplitude*cos(-TWO_PI*(float)(i+phase_index)*oneoverfs*tone_Hz));
        Q[i] = (int16_t)(32768.0*amplitude*sin(-TWO_PI*(float)(i+phase_index)*oneoverfs*tone_Hz));
    }
    return (phase_index+Nsamples);
}


int32_t FindMax(char *filename, uint32_t Nsamples){
    FILE* file = fopen(filename, "r");
    int buffer[Nsamples] = {0};
    int count = 0;
    for (size_t k=0; k<Nsamples; k++){
        fscanf(file, "%d", &buffer[k]);
    }
    int32_t max_I = get_max(&buffer[Nsamples-2048], 2048);
    return max_I;
}

TEST(FrontPanel, VolumeIncrease){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_out_L.setName("VolumeIncrease_ReceiveOut_L.txt");
    Q_out_R.setName(nullptr);
    //setfilename("VolumeIncrease");

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    EEPROMData.agc = AGCOff;
    EEPROMData.nrOptionSelect = NROff;
    InitializeSignalProcessing();

    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int32_t tmp = EEPROMData.audioVolume;
    SetInterrupt(iVOLUME_INCREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    EXPECT_EQ(EEPROMData.audioVolume,tmp+1);

    FILE* file = fopen("VolumeIncrease_ReceiveOut_L.txt", "r");
    int buffer[2048*8] = {0};
    int count = 0;
    for (size_t k=0; k<2048*8; k++){
        fscanf(file, "%d", &buffer[k]);
    }
    int32_t max_I = get_max(&buffer[2048*7], 2048);

    // Max when volume is nominal is 109
    EXPECT_GT(max_I,109);
}

TEST(FrontPanel, VolumeDecrease){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_out_L.setName("VolumeDecrease_ReceiveOut_L.txt");
    Q_out_R.setName(nullptr);

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    EEPROMData.agc = AGCOff;
    EEPROMData.nrOptionSelect = NROff;
    InitializeSignalProcessing();

    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int32_t tmp = EEPROMData.audioVolume;
    SetInterrupt(iVOLUME_DECREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    EXPECT_EQ(EEPROMData.audioVolume,tmp-1);

    FILE* file = fopen("VolumeDecrease_ReceiveOut_L.txt", "r");
    int buffer[2048*8] = {0};
    int count = 0;
    for (size_t k=0; k<2048*8; k++){
        fscanf(file, "%d", &buffer[k]);
    }
    int32_t max_I = get_max(&buffer[2048*7], 2048);

    // Max when volume is nominal is 109
    EXPECT_LT(max_I,109);
}



TEST(FrontPanel, FilterDecrease){

    // Generate an IQ tone to pass through the system for evaluation
    uint16_t Nsamples = 8192;
    int16_t I[Nsamples];
    int16_t Q[Nsamples];
    uint32_t sampleRate_Hz = 192000;

    // Default band is the 40m band. The lines below generate a tone in the LSB.
    // fshift/4 moves data 48000 hz to the right.
    int16_t tone_Hz = 2000;
    int phase = 0;
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, tone_Hz, phase, 0.9);
    Q_in_L.setChannel(0,I);
    Q_in_R.setChannel(0,Q);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_out_L.setName("FilterDecrease_ReceiveOut_L.txt");
    Q_out_R.setName(nullptr);
    //setfilename("FilterDecrease");

    EEPROMData.fineTuneFreq_Hz = -48000.0;
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    EEPROMData.agc = AGCOff;
    EEPROMData.nrOptionSelect = NROff;
   
    InitializeSignalProcessing();
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int32_t tmphi = bands[EEPROMData.currentBand].FHiCut_Hz;
    int32_t tmplo = bands[EEPROMData.currentBand].FLoCut_Hz;

    // Get the amplitude of the output tone:
    int32_t maxpre = FindMax("FilterDecrease_ReceiveOut_L.txt", 2048*4);

    // Decrease filter by 2500 Hz to cut off 2 kHz tone. It goes in steps
    // of 10 each time, and is 3000 Hz at the start. So do this 250 times.
    for (size_t k = 0; k < 250; k++){
        SetInterrupt(iFILTER_DECREASE);
        loop();
    }
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int32_t maxpost = FindMax("FilterDecrease_ReceiveOut_L.txt", 2048*258); //4238
    EXPECT_LT(maxpost, maxpre*0.3);

    if (bands[EEPROMData.currentBand].mode == USB){
        EXPECT_LT(bands[EEPROMData.currentBand].FHiCut_Hz,tmphi);
        EXPECT_EQ(bands[EEPROMData.currentBand].FHiCut_Hz,500);
    }
    if (bands[EEPROMData.currentBand].mode == LSB){
        EXPECT_GT(bands[EEPROMData.currentBand].FLoCut_Hz,tmplo);
        EXPECT_EQ(bands[EEPROMData.currentBand].FLoCut_Hz,-500);
    }
}


TEST(FrontPanel, FilterIncrease){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_out_L.setName("FilterIncrease_ReceiveOut_L.txt");
    Q_out_R.setName(nullptr);

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    EEPROMData.agc = AGCOff;
    EEPROMData.nrOptionSelect = NROff;
    InitializeSignalProcessing();
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int32_t tmphi = bands[EEPROMData.currentBand].FHiCut_Hz;
    int32_t tmplo = bands[EEPROMData.currentBand].FLoCut_Hz;
    SetInterrupt(iFILTER_INCREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    if (bands[EEPROMData.currentBand].mode == USB){
        EXPECT_GT(bands[EEPROMData.currentBand].FHiCut_Hz,tmphi);

    }
    if (bands[EEPROMData.currentBand].mode == LSB){
        EXPECT_LT(bands[EEPROMData.currentBand].FLoCut_Hz,tmplo);
    }
}


TEST(FrontPanel, CenterTuneIncrease){
    // I can't properly test this until the RF board works
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    InitializeSignalProcessing();
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int64_t tmpfreq = EEPROMData.centerFreq_Hz;
    SetInterrupt(iCENTERTUNE_INCREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    EXPECT_EQ(EEPROMData.centerFreq_Hz,tmpfreq+EEPROMData.freqIncrement);
}

TEST(FrontPanel, CenterTuneDecrease){
    // I can't properly test this until the RF board works
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    InitializeSignalProcessing();
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int64_t tmpfreq = EEPROMData.centerFreq_Hz;
    SetInterrupt(iCENTERTUNE_DECREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    EXPECT_EQ(EEPROMData.centerFreq_Hz,tmpfreq-EEPROMData.freqIncrement);
}

TEST(FrontPanel, FineTuneIncrease){
    //EXPECT_EQ(1,0);
}

TEST(FrontPanel, FineTuneDecrease){
    //EXPECT_EQ(1,0);
}

