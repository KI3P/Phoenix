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

TEST(FrontPanel, VolumeIncrease){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_out_L.setName("VolumeIncrease_ReceiveOut_L.txt");
    Q_out_R.setName("VolumeIncrease_ReceiveOut_R.txt");

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    EEPROMData.agc = AGCOff;
    EEPROMData.nrOptionSelect = NROff;
    InitializeFilters(EEPROMData.spectrum_zoom,&filters);
    InitializeAGC(&agc,SR[SampleRate].rate/filters.DF);

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
    Q_out_R.setName("VolumeDecrease_ReceiveOut_R.txt");

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    EEPROMData.agc = AGCOff;
    EEPROMData.nrOptionSelect = NROff;
    InitializeFilters(EEPROMData.spectrum_zoom,&filters);
    InitializeAGC(&agc,SR[SampleRate].rate/filters.DF);

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

TEST(FrontPanel, FilterIncrease){
    //EXPECT_EQ(1,0);
}

TEST(FrontPanel, FilterDecrease){
    //EXPECT_EQ(1,0);
}

TEST(FrontPanel, CenterTuneIncrease){
    //EXPECT_EQ(1,0);
}

TEST(FrontPanel, CenterTuneDecrease){
    //EXPECT_EQ(1,0);
}

TEST(FrontPanel, FineTuneIncrease){
    //EXPECT_EQ(1,0);
}

TEST(FrontPanel, FineTuneDecrease){
    //EXPECT_EQ(1,0);
}

