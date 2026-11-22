#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"
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

/*TEST(FrontPanel, VolumeIncrease){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_out_L.setName("VolumeIncrease_ReceiveOut_L.txt");
    Q_out_R.setName(nullptr);
    //setfilename("VolumeIncrease");

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    uiSM.state_id = UISm_StateId_HOME;
    ED.agc = AGCOff;
    ED.nrOptionSelect = NROff;
    InitializeSignalProcessing();

    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int32_t tmp = ED.audioVolume;
    SetInterrupt(iVOLUME_INCREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    EXPECT_EQ(ED.audioVolume,tmp+1);

    FILE* file = fopen("VolumeIncrease_ReceiveOut_L.txt", "r");
    int buffer[2048*8] = {0};
    int count = 0;
    for (size_t k=0; k<2048*8; k++){
        fscanf(file, "%d", &buffer[k]);
    }
    int32_t max_I = get_max(&buffer[2048*7], 2048);

    // Max when volume is nominal is 109
    EXPECT_GT(max_I,109);
}*/

TEST(FrontPanel, VolumeDecrease){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_out_L.setName("VolumeDecrease_ReceiveOut_L.txt");
    Q_out_R.setName(nullptr);

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    uiSM.state_id = UISm_StateId_HOME;
    ED.agc = AGCOff;
    ED.nrOptionSelect = NROff;
    InitializeSignalProcessing();

    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int32_t tmp = ED.audioVolume;
    SetInterrupt(iVOLUME_DECREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    EXPECT_EQ(ED.audioVolume,tmp-1);

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

    ED.fineTuneFreq_Hz[ED.activeVFO] = -48000.0;
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    uiSM.state_id = UISm_StateId_HOME;
    ED.agc = AGCOff;
    ED.nrOptionSelect = NROff;
    ED.currentBand[ED.activeVFO] = BAND_20M;
   
    InitializeSignalProcessing();
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int32_t tmphi = bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
    int32_t tmplo = bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz;

    // Get the amplitude of the output tone:
    int32_t maxpre = FindMax("FilterDecrease_ReceiveOut_L.txt", 2048*4);

    // Decrease filter by 2500 Hz to cut off 2 kHz tone. It goes in steps
    // of 50 each time, and is 3000 Hz at the start. So do this 50 times.
    for (size_t k = 0; k < 50; k++){
        SetInterrupt(iFILTER_DECREASE);
        loop();
    }
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int32_t maxpost = FindMax("FilterDecrease_ReceiveOut_L.txt", 2048*258); //4238
    EXPECT_LT(maxpost, maxpre*0.5);

    EXPECT_EQ(bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz,500);

}


TEST(FrontPanel, FilterIncrease){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_out_L.setName("FilterIncrease_ReceiveOut_L.txt");
    Q_out_R.setName(nullptr);

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    uiSM.state_id = UISm_StateId_HOME;
    ED.agc = AGCOff;
    ED.nrOptionSelect = NROff;
    InitializeSignalProcessing();
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int32_t tmphi = bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
    int32_t tmplo = bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz;
    SetInterrupt(iFILTER_INCREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    if (bands[ED.currentBand[ED.activeVFO]].mode == USB){
        EXPECT_GT(bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz,tmphi);

    }
    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB){
        EXPECT_LT(bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz,tmplo);
    }
}


TEST(FrontPanel, CenterTuneIncrease){
    // I can't properly test this until the RF board works
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    uiSM.state_id = UISm_StateId_HOME;
    InitializeSignalProcessing();
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int64_t tmpfreq = ED.centerFreq_Hz[ED.activeVFO];
    SetInterrupt(iCENTERTUNE_INCREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    EXPECT_EQ(ED.centerFreq_Hz[ED.activeVFO],tmpfreq+ED.freqIncrement);
}

TEST(FrontPanel, CenterTuneDecrease){
    // I can't properly test this until the RF board works
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    uiSM.state_id = UISm_StateId_HOME;
    InitializeSignalProcessing();
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int64_t tmpfreq = ED.centerFreq_Hz[ED.activeVFO];
    SetInterrupt(iCENTERTUNE_DECREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    EXPECT_EQ(ED.centerFreq_Hz[ED.activeVFO],tmpfreq-ED.freqIncrement);
}

TEST(FrontPanel, FineTuneIncrease){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    uiSM.state_id = UISm_StateId_HOME;
    InitializeSignalProcessing();
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int64_t tmpfreq = ED.fineTuneFreq_Hz[ED.activeVFO];
    SetInterrupt(iFINETUNE_INCREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    EXPECT_EQ(ED.fineTuneFreq_Hz[ED.activeVFO],tmpfreq+ED.stepFineTune);
}

TEST(FrontPanel, FineTuneDecrease){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    uiSM.state_id = UISm_StateId_HOME;
    InitializeSignalProcessing();
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    int64_t tmpfreq = ED.fineTuneFreq_Hz[ED.activeVFO];
    SetInterrupt(iFINETUNE_DECREASE);
    for (size_t k = 0; k < 4; k++){
        loop();
    }
    EXPECT_EQ(ED.fineTuneFreq_Hz[ED.activeVFO],tmpfreq-ED.stepFineTune);
}

TEST(FrontPanel, FineTuneLimits){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.spectrum_zoom = 1;
    ED.modulation[ED.activeVFO] = LSB;
    InitializeSignalProcessing();

    uint32_t visible_bandwidth = SR[SampleRate].rate / (1 << ED.spectrum_zoom);
    int32_t upper_limit = (int32_t)visible_bandwidth/2;
    int32_t lower_limit = -(int32_t)visible_bandwidth/2;

    // since we have LSB
    lower_limit -= bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz; // FLoCut_Hz is negative

    // The AdjustFineTune function negates the limits when clamping (see Tune.cpp:80-83)
    // So the actual fineTuneFreq_Hz limits are negated versions of the calculated limits
    int32_t actual_upper_limit = -lower_limit;  // When going high, gets clamped to -lower_limit
    int32_t actual_lower_limit = -upper_limit;  // When going low, gets clamped to -upper_limit

    // Test upper limit
    ED.fineTuneFreq_Hz[ED.activeVFO] = actual_upper_limit;
    SetInterrupt(iFINETUNE_INCREASE);
    loop();
    EXPECT_EQ(ED.fineTuneFreq_Hz[ED.activeVFO], actual_upper_limit);

    // Test lower limit
    ED.fineTuneFreq_Hz[ED.activeVFO] = actual_lower_limit;
    SetInterrupt(iFINETUNE_DECREASE);
    loop();
    EXPECT_EQ(ED.fineTuneFreq_Hz[ED.activeVFO], actual_lower_limit);
}

TEST(FrontPanel, FineTuneLimitsNoZoom){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.spectrum_zoom = 0;
    InitializeSignalProcessing();

    uint32_t visible_bandwidth = SR[SampleRate].rate / (1 << ED.spectrum_zoom);
    int32_t upper_limit = (int32_t)visible_bandwidth/2;
    int32_t lower_limit = -(int32_t)visible_bandwidth/2;

    // Even when zoom is 0, the modulation-based filter adjustments are applied (Tune.cpp:60-77)
    // We need to account for the current modulation's filter settings
    switch (ED.modulation[ED.activeVFO]){
        case LSB:
            lower_limit -= bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz; // FLoCut_Hz is negative
            break;
        case USB:
            upper_limit -= bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
            break;
        default:
            // For other modes, symmetric adjustment would be applied
            break;
    }

    // The AdjustFineTune function negates the limits when clamping (see Tune.cpp:80-83)
    // So the actual fineTuneFreq_Hz limits are negated versions of the calculated limits
    int32_t actual_upper_limit = -lower_limit;  // When going high, gets clamped to -lower_limit
    int32_t actual_lower_limit = -upper_limit;  // When going low, gets clamped to -upper_limit

    // Test upper limit
    ED.fineTuneFreq_Hz[ED.activeVFO] = actual_upper_limit;
    SetInterrupt(iFINETUNE_INCREASE);
    loop();
    EXPECT_EQ(ED.fineTuneFreq_Hz[ED.activeVFO], actual_upper_limit);

    // Test lower limit
    ED.fineTuneFreq_Hz[ED.activeVFO] = actual_lower_limit;
    SetInterrupt(iFINETUNE_DECREASE);
    loop();
    EXPECT_EQ(ED.fineTuneFreq_Hz[ED.activeVFO], actual_lower_limit);
}
// ---------------------------------------------------------------------------
// Rotary encoder decoder. Contact states are (B << 1) | A, 1 = open; the
// encoder rests at 0b11. A leads: 11 10 00 01 11. B leads: 11 01 00 10 11.
// ---------------------------------------------------------------------------
#include "../src/PhoenixSketch/FrontPanel_Rotary.h"
#include <vector>

static int FeedEncoder(Rotary_V12 &enc, const std::vector<unsigned char> &states){
    int total = 0;
    for (unsigned char s : states){
        enc.update(s);
        total += enc.process();
    }
    return total;
}

static const std::vector<unsigned char> A_LEADS = {0b10, 0b00, 0b01, 0b11};
static const std::vector<unsigned char> B_LEADS = {0b01, 0b00, 0b10, 0b11};

TEST(RotaryEncoder, CleanDetentCountsOneStepEachWay){
    Rotary_V12 enc(false);
    EXPECT_EQ(FeedEncoder(enc, A_LEADS), +1);
    EXPECT_EQ(FeedEncoder(enc, B_LEADS), -1);
    Rotary_V12 rev(true);
    EXPECT_EQ(FeedEncoder(rev, A_LEADS), -1);
    EXPECT_EQ(FeedEncoder(rev, B_LEADS), +1);
}

TEST(RotaryEncoder, StepIsCountedOnlyWhenTheDetentCompletes){
    Rotary_V12 enc(false);
    EXPECT_EQ(FeedEncoder(enc, {0b10, 0b00, 0b01}), 0);
    EXPECT_EQ(FeedEncoder(enc, {0b11}), +1);
}

TEST(RotaryEncoder, ChatterOnTheClosingContactCountsOneStep){
    // The fault measured on the Filter knob: in a B-leads detent contact A
    // reopens and closes again many times as it closes
    std::vector<unsigned char> seq = {0b01, 0b00};
    for (int i = 0; i < 20; i++){ seq.push_back(0b01); seq.push_back(0b00); }
    seq.push_back(0b10);
    seq.push_back(0b11);
    Rotary_V12 enc(true);
    EXPECT_EQ(FeedEncoder(enc, seq), +1);
}

TEST(RotaryEncoder, ChatterOnEitherContactAtAnyPointCountsOneStep){
    // Every contact change in an A-leads detent bounces three times
    std::vector<unsigned char> seq;
    unsigned char prev = 0b11;
    for (unsigned char s : A_LEADS){
        for (int i = 0; i < 3; i++){ seq.push_back(s); seq.push_back(prev); }
        seq.push_back(s);
        prev = s;
    }
    Rotary_V12 enc(false);
    EXPECT_EQ(FeedEncoder(enc, seq), +1);
}

TEST(RotaryEncoder, DetentStartedAndAbandonedCountsNothing){
    Rotary_V12 enc(false);
    EXPECT_EQ(FeedEncoder(enc, {0b10, 0b00, 0b10, 0b11}), 0);
    EXPECT_EQ(FeedEncoder(enc, {0b01, 0b00, 0b01, 0b11}), 0);
}

TEST(RotaryEncoder, SkippedStateDuringAFastSpinStillCounts){
    // With the front panel read every 1 ms, a fast spin can change both
    // contacts between readings
    Rotary_V12 enc(false);
    EXPECT_EQ(FeedEncoder(enc, {0b10, 0b01, 0b11}), +1);   // 00 skipped
    EXPECT_EQ(FeedEncoder(enc, {0b10, 0b00, 0b11}), +1);   // 01 skipped
    EXPECT_EQ(FeedEncoder(enc, {0b10, 0b00, 0b01, 0b10, 0b00, 0b01, 0b11}), +2); // 11 skipped
    EXPECT_EQ(FeedEncoder(enc, {0b01, 0b10, 0b11}), -1);
    EXPECT_EQ(FeedEncoder(enc, {0b01, 0b00, 0b10, 0b01, 0b00, 0b10, 0b11}), -2);
}

TEST(RotaryEncoder, MissedFirstEdgeResumesTheLastDirection){
    Rotary_V12 enc(false);
    EXPECT_EQ(FeedEncoder(enc, B_LEADS), -1);
    // The first contact closing was never seen: rest straight to both closed
    EXPECT_EQ(FeedEncoder(enc, {0b00, 0b10, 0b11}), -1);
}

TEST(RotaryEncoder, MissedFirstEdgeAfterAReversalCountsNothing){
    Rotary_V12 enc(false);
    EXPECT_EQ(FeedEncoder(enc, B_LEADS), -1);
    // The knob now turns the other way and its first edge is missed
    EXPECT_EQ(FeedEncoder(enc, {0b00, 0b01, 0b11}), 0);
    // The next detent that way counts normally
    EXPECT_EQ(FeedEncoder(enc, A_LEADS), +1);
}

TEST(RotaryEncoder, MissedFirstEdgeBeforeAnyStepCountsNothing){
    Rotary_V12 enc(false);
    EXPECT_EQ(FeedEncoder(enc, {0b00, 0b10, 0b11}), 0);
}

TEST(RotaryEncoder, EncodersKeepTheirOwnState){
    // Interleaved halves of a detent on two encoders do not interfere
    Rotary_V12 one(false), two(false);
    one.update(0b10); two.update(0b01);
    one.update(0b00); two.update(0b00);
    one.update(0b01); two.update(0b10);
    one.update(0b11); two.update(0b11);
    EXPECT_EQ(one.process(), +1);
    EXPECT_EQ(two.process(), -1);
}

TEST(RotaryEncoder, RecordedFilterKnobCountsOneStepPerDetent){
    // The contact states the firmware read from the KI3P prototype's Filter
    // knob (reversed) on 2026-11-17: five clockwise detents, a small movement
    // back to rest, then five anticlockwise detents. The old decoder counted
    // +19, +6, 0, +2, 0 for the clockwise ones. Clockwise detent 3 lost the
    // reading of contact B closing.
    const std::vector<std::vector<unsigned char>> detents = {
        {0b11,0b01,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b10,0b11,0b01,0b01,0b01,0b01,0b11,0b01,0b01,0b11,0b11,0b11,0b01,0b01,0b01,0b11,0b01,0b11,0b01},
        {0b00,0b00,0b00,0b00,0b01,0b01,0b00,0b00,0b10,0b11,0b01,0b11,0b01,0b11,0b01,0b11,0b11,0b11},
        {0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b00,0b10,0b11,0b01,0b01,0b11,0b01},
        {0b01,0b01,0b01,0b00,0b01,0b00,0b10,0b11},
        {0b11,0b01,0b00,0b10,0b11,0b01},
        {0b11,0b11},
        {0b10,0b00,0b01,0b11},
        {0b10,0b00,0b01,0b01,0b11},
        {0b10,0b00,0b00,0b01,0b11},
        {0b10,0b00,0b00,0b01,0b11,0b11},
        {0b10,0b00,0b00,0b01,0b01,0b11},
    };
    const int expected[] = {+1, +1, +1, +1, +1, 0, -1, -1, -1, -1, -1};
    Rotary_V12 filter(FILTER_REVERSED);
    for (size_t i = 0; i < detents.size(); i++)
        EXPECT_EQ(FeedEncoder(filter, detents[i]), expected[i]) << "detent " << i;
}
