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

int CreateIQToneWithPhase(float *I, float *Q, int Nsamples, int sampleRate_Hz,
                int tone_Hz, int phase_index,float amplitude){
    float oneoverfs = 1.0 / (float)sampleRate_Hz;
    for (int i=0;i<Nsamples;i++){
        I[i] = amplitude*cos(-TWO_PI*(float)(i+phase_index)*oneoverfs*tone_Hz);
        Q[i] = amplitude*sin(-TWO_PI*(float)(i+phase_index)*oneoverfs*tone_Hz);
    }
    return (phase_index+Nsamples);
}

int AddIQToneWithPhase(float *I, float *Q, int Nsamples, int sampleRate_Hz,
                int tone_Hz, int phase_index,float amplitude){
    float oneoverfs = 1.0 / (float)sampleRate_Hz;
    for (int i=0;i<Nsamples;i++){
        I[i] += amplitude*cos(-TWO_PI*(float)(i+phase_index)*oneoverfs*tone_Hz);
        Q[i] += amplitude*sin(-TWO_PI*(float)(i+phase_index)*oneoverfs*tone_Hz);
    }
    return (phase_index+Nsamples);
}

void CreateTone(float *buf, int Nsamples, int sampleRate_Hz, float tone_Hz){
    float oneoverfs = 1.0 / (float)sampleRate_Hz;
    for (int i=0;i<Nsamples;i++){
        buf[i] = 0.5*sin(TWO_PI*tone_Hz*(float)i*oneoverfs);
    }
}

void CreateIQTone(float *I, float *Q, int Nsamples, int sampleRate_Hz, float tone_Hz){
    float oneoverfs = 1.0 / (float)sampleRate_Hz;
    for (int i=0;i<Nsamples;i++){
        I[i] = 0.5*cos(TWO_PI*tone_Hz*(float)i*oneoverfs);
        Q[i] = 0.5*sin(TWO_PI*tone_Hz*(float)i*oneoverfs);
    }
}

void CreateIQChirp(float *I, float *Q, int Nsamples, int sampleRate_Hz){
    float oneoverfs = 1.0 / (float)sampleRate_Hz;
    for (int i=0;i<Nsamples;i++){
        I[i] = 0.5*cos(-TWO_PI*(float)i*oneoverfs*(200+(float)i/2048*1000));
        Q[i] = 0.5*sin(-TWO_PI*(float)i*oneoverfs*(200+(float)i/2048*1000));
    }
}

void CreateDoubleTone(float *buf, int Nsamples, int sampleRate_Hz, 
                        float tone1_Hz,float tone2_Hz){
    float oneoverfs = 1.0 / (float)sampleRate_Hz;
    for (int i=0;i<Nsamples;i++){
        buf[i] = 0.5*sin(TWO_PI*tone1_Hz*(float)i*oneoverfs)
                +0.5*sin(TWO_PI*tone2_Hz*(float)i*oneoverfs);
    }
}

void WriteFile(float32_t *data, char *fname, int N){
    FILE *file = fopen(fname, "w");
    for (size_t i = 0; i < N; i++) {
        fprintf(file, "%d,%7.6f\n", i,data[i]);
    }
    fclose(file);
}

void WriteIQFile(float32_t *I, float32_t *Q, char *fname, int N){
    FILE *file = fopen(fname, "w");
    for (size_t i = 0; i < N; i++) {
        fprintf(file, "%d,%7.6f,%7.6f\n", i,I[i],Q[i]);
    }
    fclose(file);
}

void WriteBiquadFilterState(arm_biquad_casd_df1_inst_f32 *bq, char *fname){
    FILE *file = fopen(fname, "w");
    fprintf(file,"Num stages:        %d\n",bq->numStages);
    fprintf(file,"pState pointer:    %ld\n",bq->pState);
    fprintf(file,"pCoeffs pointer:   %ld\n",bq->pCoeffs);
    fprintf(file, "Stage, pstate 1,2,3,4\n");
    for (int i=0; i<bq->numStages; i++){
        fprintf(file,"    %d,%6.5f,%6.5f,%6.5f,%6.5f\n",i,
                bq->pState[0],
                bq->pState[1],
                bq->pState[2],
                bq->pState[3]);
    }
    fprintf(file, "Stage, coeffs 1,2,3,4,5\n");
    for (int i=0; i<bq->numStages; i++){
        fprintf(file,"    %d,%6.5f,%6.5f,%6.5f,%6.5f,%6.5f\n",i,
                bq->pCoeffs[5*i+0],
                bq->pCoeffs[5*i+1],
                bq->pCoeffs[5*i+2],
                bq->pCoeffs[5*i+3],
                bq->pCoeffs[5*i+4]);
    }
    fclose(file);
}

void PrepareIQDataFsOver4Tone(float *I, float *Q, float *buffer_spec_FFT){
    for (size_t i = 0; i<128; i++){
        I[4*i+0] = +1;
        I[4*i+1] =  0;
        I[4*i+2] = -1;
        I[4*i+3] =  0;

        Q[4*i+0] =  0;
        Q[4*i+1] = -1;
        Q[4*i+2] =  0;
        Q[4*i+3] = +1;
    }
    for (size_t i = 0; i < SPECTRUM_RES; i++) { 
        buffer_spec_FFT[i * 2] =      I[i]; 
        buffer_spec_FFT[i * 2 + 1] =  Q[i];
    }
}

void PrepareIQDataFsOver4Tone(float *I, float *Q, uint32_t Nsamples){
    for (size_t i = 0; i<Nsamples/4; i++){
        I[4*i+0] = +1;
        I[4*i+1] =  0;
        I[4*i+2] = -1;
        I[4*i+3] =  0;

        Q[4*i+0] =  0;
        Q[4*i+1] = -1;
        Q[4*i+2] =  0;
        Q[4*i+3] = +1;
    }
}

int32_t frequency_to_bin(float freq, int32_t Nbins, int32_t SampleRate){
    return (Nbins/2 + (int32_t)( (float)Nbins * freq / (float)SampleRate ));
}


void add_second_tone(float *I, float *Q, float tone2_Hz, int sampleRate_Hz, int Nsamples){
    float I2[Nsamples];
    float Q2[Nsamples];
    CreateIQTone(I2, Q2, Nsamples, sampleRate_Hz, tone2_Hz);
    for (int i =0; i<Nsamples; i++){
        I[i] += I2[i];
        Q[i] += Q2[i];
    }
}

void add_comb(float *I, float *Q, int sampleRate_Hz, int Nsamples){
    float tone2_Hz = 96000-10*96000/512;
    //add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    //tone2_Hz = 96000-50*96000/512;
    //add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    //tone2_Hz = 96000-90*96000/512;
    //add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    //tone2_Hz = 96000-130*96000/512;
    //add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    //tone2_Hz = 96000-210*96000/512;
    //add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    tone2_Hz = 96000-290*96000/512;
    add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    tone2_Hz = 96000-370*96000/512;
    add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    tone2_Hz = 96000-450*96000/512;
    add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
}

TEST(TransmitChain, DecimateByX){
    // For all the decimate by N tests, run two blocks through because the state
    // vector for the FIR filter starts at zero for the first block, which introduces
    // artifacts
    uint32_t Nsamples = 2048*2;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];
    float tone_Hz;
    tone_Hz = -10*96000/512;

    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    add_comb(I,Q,sampleRate_Hz,Nsamples);
    WriteIQFile(I,Q,"TXDecimateBy4_original_IQ.txt",Nsamples);
    TXDecInit();
    TXDecimateBy4(I,Q);
    WriteIQFile(I,Q,"TXDecimateBy4_decimated_IQ_pass1.txt",Nsamples/4/2);
    TXDecimateBy4(&I[2048],&Q[2048]);
    WriteIQFile(&I[2048],&Q[2048],"TXDecimateBy4_decimated_IQ_pass2.txt",Nsamples/4/2);

    CreateIQTone(I, Q, 1024, sampleRate_Hz/4, tone_Hz);
    add_comb(I,Q,sampleRate_Hz/4,1024);
    TXDecInit();
    WriteIQFile(I,Q,"TXDecimateBy2_original_IQ.txt",1024);
    TXDecimateBy2(I,Q);
    WriteIQFile(I,Q,"TXDecimateBy2_decimated_IQ_pass1.txt",256);
    TXDecimateBy2(&I[512],&Q[512]);
    WriteIQFile(&I[512],&Q[512],"TXDecimateBy2_decimated_IQ_pass2.txt",256);

    CreateIQTone(I, Q, 512, sampleRate_Hz/8, tone_Hz);
    add_comb(I,Q,sampleRate_Hz/8,512);
    TXDecInit();
    WriteIQFile(I,Q,"TXDecimateBy16_original_IQ.txt",512);
    TXDecimateBy2(I,Q);
    WriteIQFile(I,Q,"TXDecimateBy16_decimated_IQ_pass1.txt",128);
    TXDecimateBy2(&I[256],&Q[256]);
    WriteIQFile(&I[256],&Q[256],"TXDecimateBy16_decimated_IQ_pass2.txt",128);

    EXPECT_EQ(1,1);
}

float32_t getmax(float32_t *data, uint32_t N){
    float32_t amp = -FLT_MAX;
    for (size_t i=0; i<N; i++){
        if (data[i] > amp) amp = data[i];
    }
    return amp;
}

TEST(TransmitChain, DecimateByXTransmissionResponse){
    // For all the decimate by N tests, run two blocks through because the state
    // vector for the FIR filter starts at zero for the first block, which introduces
    // artifacts
    uint32_t Nsamples = 2048*2;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];

    float32_t fmin = 0.0;
    float32_t fmax = 90000.0;
    uint32_t Npoints = 101;
    float32_t fstep = (fmax - fmin)/(float32_t)Npoints;
    float32_t gainx4[Npoints];
    float32_t gainx2[Npoints];
    float32_t gainx2x2[Npoints];
    float32_t freq[Npoints];   

    TXDecInit();

    float32_t amp;
    for (int i = 0; i<Npoints; i++){
        freq[i] = fmin + (float32_t)i*fstep;

        CreateIQTone(I, Q, Nsamples, sampleRate_Hz, freq[i]);

        TXDecimateBy4(I,Q);
        TXDecimateBy4(&I[2048],&Q[2048]);
        amp = getmax(&I[2048],Nsamples/4/2);
        gainx4[i] = amp/0.5;
    }
    WriteIQFile(freq,gainx4,"TXDecimateBy4_passband.txt",Npoints);

    CreateIQTone(I, Q, 1024, 192000/4, 20000);
    TXDecimateBy2(I,Q);
    TXDecimateBy2(&I[512],&Q[512]);
    WriteIQFile(&I[512],&Q[512],"TXDecimateBy2_6000.txt",256);
    for (int i = 0; i<Npoints; i++){
        freq[i] = fmin + (float32_t)i*fstep;

        CreateIQTone(I, Q, 1024, 192000/4, freq[i]);

        TXDecimateBy2(I,Q);
        TXDecimateBy2(&I[512],&Q[512]);

        gainx2[i] = getmax(&I[512+128],128)/0.5;
    }
    WriteIQFile(freq,gainx2,"TXDecimateBy2_passband.txt",Npoints);


    for (int i = 0; i<Npoints; i++){
        freq[i] = fmin + (float32_t)i*fstep;

        CreateIQTone(I, Q, 512, 192000/8, freq[i]);

        TXDecimateBy2(I,Q);
        TXDecimateBy2(&I[256],&Q[256]);

        gainx2x2[i] = getmax(&I[256+64],64)/0.5;
    }
    WriteIQFile(freq,gainx2x2,"TXDecimateBy2x2_passband.txt",Npoints);

    EXPECT_EQ(1,1);
}

TEST(TransmitChain, HilbertPassband){
    uint32_t Nsamples = 128*2;
    uint32_t sampleRate_Hz = 192000/4/2/2;
    float I[Nsamples];
    float Q[Nsamples];

    float32_t fmin = 0.0;
    float32_t fmax = 6000.0;
    uint32_t Npoints = 101;
    float32_t fstep = (fmax - fmin)/(float32_t)Npoints;
    float32_t hilbertI[Npoints];
    float32_t hilbertQ[Npoints];
    float32_t mag[Npoints];
    float32_t freq[Npoints];   

    TXDecInit();
    float32_t amp;
    for (int i = 0; i<Npoints; i++){
        freq[i] = fmin + (float32_t)i*fstep;
        CreateIQTone(I, Q, Nsamples, sampleRate_Hz, freq[i]);
        arm_copy_f32(I,Q,Nsamples);

        HilbertTransform(I,Q);
        HilbertTransform(&I[128],&Q[128]);

        mag[i] = 0;
        for (int j=0; j<Nsamples/2; j++){
            mag[i] += sqrtf(Q[128+j]*Q[128+j] + I[128+j]*I[128+j]);
        }
        mag[i] = mag[i]/(Nsamples/2);
        
        if (i == Npoints-1){
            float32_t angle[Nsamples/2];
            float32_t mag =0;
            for (int j=0; j<Nsamples/2; j++){
                angle[j] = atan2f(Q[128+j],I[128+j]);
            }
            WriteFile(angle,"TXHilbert_angle.txt",Nsamples/2);
            WriteIQFile(&I[128],&Q[128],"TXHilbert_post_IQ.txt",128);
        }
        hilbertI[i] = getmax(&I[128],128)/0.5;
        hilbertQ[i] = getmax(&Q[128],128)/0.5;
    }
    WriteIQFile(freq,hilbertI,"TXHilbertI_passband.txt",Npoints);
    WriteIQFile(freq,hilbertQ,"TXHilbertQ_passband.txt",Npoints);
    WriteIQFile(freq,mag,"TXHilbertIQ_mag.txt",Npoints);    

    EXPECT_EQ(1,1);
}

void TXEQ_filter_tone(float32_t toneFreq_Hz, float32_t *gain){
   
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;
   
    uint32_t phase = 0;
    // Need four iterations for the filter to "warm up" -- i.e., for the IIR filter state vector
    // to reach equilibrium -- at the lowest bands
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    DoExciterEQ(I);
    
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    DoExciterEQ(I);

    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    DoExciterEQ(I);

    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    DoExciterEQ(I);

    // Find the max value in I
    float32_t amp = -FLT_MAX;
    for (size_t i=0; i<256; i++){
        if (I[i] > amp) amp = I[i];
    }
    *gain = amp/0.1;
}

void TXEQ_filter_tone_new(float32_t toneFreq_Hz, float32_t *gain){
   
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);

    uint32_t phase = 0;
    // Need four iterations for the filter to "warm up" -- i.e., for the IIR filter state vector
    // to reach equilibrium -- at the lowest bands
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    BandEQ(&data,&filters,TX);
    
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    BandEQ(&data,&filters,TX);
    
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    BandEQ(&data,&filters,TX);
    
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    BandEQ(&data,&filters,TX);
    
    // Find the max value in I
    float32_t amp = -FLT_MAX;
    for (size_t i=0; i<256; i++){
        if (I[i] > amp) amp = I[i];
    }
    *gain = amp/0.1;
}


TEST(TransmitChain, TransmitEQPlotPassbands){
    // Generate a bunch of tones between fmin and fmax. Measure the output of
    // the filtered data for each tone. Measure the passband in this way.

    float32_t fmin = 100.0;
    float32_t fmax = 8000.0;
    uint32_t Npoints = 401;
    float32_t fstep = (fmax - fmin)/(float32_t)Npoints;
    float32_t gain[Npoints];
    float32_t freq[Npoints];
    char strbuf[50];

    for (int i = 0; i<Npoints; i++){
        freq[i] = fmin + (float32_t)i*fstep;
        TXEQ_filter_tone(freq[i], &gain[i]);
    }
    sprintf(strbuf,"TransmitEQ_orig.txt");
    WriteIQFile(freq,gain,strbuf,Npoints);

    for (int i = 0; i<Npoints; i++){
        freq[i] = fmin + (float32_t)i*fstep;
        TXEQ_filter_tone_new(freq[i], &gain[i]);
    }
    sprintf(strbuf,"TransmitEQ_new.txt");
    WriteIQFile(freq,gain,strbuf,Npoints);
}

TEST(TransmitChain, InterpolateByN){
    uint32_t Nsamples = 128*2;
    uint32_t sampleRate_Hz = 192000/16;
    float I[Nsamples];
    float Q[Nsamples];
    float Iout[256];
    float Qout[256];
    float tone_Hz;
    tone_Hz = 12000/32;
    tone_Hz = 12000/4;

    TXDecInit();
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    WriteIQFile(I,Q,"TXInterpolateBy2x2_original_IQ.txt",Nsamples);
    TXInterpolateBy2Again(I,Q,Iout,Qout);
    WriteIQFile(Iout,Qout,"TXInterpolateBy2x2_interpolated_IQ_pass1.txt",256);
    TXInterpolateBy2Again(&I[128],&Q[128],Iout,Qout);
    WriteIQFile(Iout,Qout,"TXInterpolateBy2x2_interpolated_IQ_pass2.txt",256);


    Nsamples = 256*2;
    sampleRate_Hz = 192000/8;
    float I2[Nsamples];
    float Q2[Nsamples];
    float Iout2[512];
    float Qout2[512];
    tone_Hz = 24000/4;
    CreateIQTone(I2, Q2, Nsamples, sampleRate_Hz, tone_Hz);
    WriteIQFile(I2,Q2,"TXInterpolateBy2_original_IQ.txt",Nsamples);
    TXInterpolateBy2(I2,Q2,Iout2,Qout2);
    WriteIQFile(Iout2,Qout2,"TXInterpolateBy2_interpolated_IQ_pass1.txt",512);
    TXInterpolateBy2(&I2[256],&Q2[256],Iout2,Qout2);
    WriteIQFile(Iout2,Qout2,"TXInterpolateBy2_interpolated_IQ_pass2.txt",512);


    Nsamples = 512*2;
    sampleRate_Hz = 192000/4;
    float I4[Nsamples];
    float Q4[Nsamples];
    float Iout4[2048];
    float Qout4[2048];
    tone_Hz = 24000/4;
    CreateIQTone(I4, Q4, Nsamples, sampleRate_Hz, tone_Hz);
    WriteIQFile(I4,Q4,"TXInterpolateBy4_original_IQ.txt",Nsamples);
    TXInterpolateBy4(I4,Q4,Iout4,Qout4);
    WriteIQFile(Iout4,Qout4,"TXInterpolateBy4_interpolated_IQ_pass1.txt",2048);
    TXInterpolateBy4(&I4[512],&Q4[512],Iout4,Qout4);
    WriteIQFile(Iout4,Qout4,"TXInterpolateBy4_interpolated_IQ_pass2.txt",2048);

    EXPECT_EQ(1,1);
}


TEST(TransmitChain, InterpolateByNPassband){
    uint32_t Nsamples = 128*2*2*4;
    uint32_t sampleRate_Hz = 192000/16;
    float I[Nsamples*2*4];
    float Q[Nsamples*2*4];
    float Iout[256*2*4];
    float Qout[256*2*4];

    float32_t fmin = 0.0;
    float32_t fmax = 48000.0;
    uint32_t Npoints = 101;
    float32_t fstep = (fmax - fmin)/(float32_t)Npoints;
    float32_t gainx4[Npoints];
    float32_t gainx2[Npoints];
    float32_t gainx2x2[Npoints];
    float32_t freq[Npoints];

    TXDecInit();

    float32_t amp;
    for (int i = 0; i<Npoints; i++){
        freq[i] = fmin + (float32_t)i*fstep;
        CreateIQTone(I, Q, Nsamples, sampleRate_Hz, freq[i]);
        TXInterpolateBy2Again(I,Q,Iout,Qout);
        TXInterpolateBy2Again(&I[128],&Q[128],Iout,Qout);
        amp = getmax(Iout,256);
        gainx2x2[i] = amp/0.5;


        CreateIQTone(I, Q, Nsamples, sampleRate_Hz*2, freq[i]);
        TXInterpolateBy2(I,Q,Iout,Qout);
        TXInterpolateBy2(&I[256],&Q[256],Iout,Qout);
        amp = getmax(Iout,512);
        gainx2[i] = amp/0.5;

        CreateIQTone(I, Q, Nsamples, sampleRate_Hz*2*4, freq[i]);
        TXInterpolateBy4(I,Q,Iout,Qout);
        TXInterpolateBy4(&I[512],&Q[512],Iout,Qout);
        amp = getmax(Iout,2048);
        gainx4[i] = amp/0.5;

    }
    WriteIQFile(freq,gainx2x2,"TXInterpolateBy2x2_passband.txt",Npoints);
    WriteIQFile(freq,gainx2,"TXInterpolateBy2_passband.txt",Npoints);
    WriteIQFile(freq,gainx4,"TXInterpolateBy4_passband.txt",Npoints);
    EXPECT_EQ(1,1);
}

void end2end(float32_t *I, float32_t *Q, float32_t *Io, float32_t *Qo){
    TXDecimateBy4(I,Q);// 2048 in, 512 out
    TXDecimateBy2(I,Q);// 512 in, 256 out
    DoExciterEQ(I); // 256
    arm_copy_f32(I,Q,256);
    TXDecimateBy2Again(I,Q); // 256 in, 128 out
    HilbertTransform(I,Q); // 128
    TXInterpolateBy2Again(I,Q,Io,Qo); // 128 in, 256 out
    SidebandSelection(Io, Qo);
    TXInterpolateBy2(Io,Qo,I,Q); // 256 in, 512 out
    TXInterpolateBy4(I,Q,Io,Qo); // 512 in, 2048 out
}

TEST(TransmitChain, EndToEnd){
    uint32_t Nsamples = 2048;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];
    float Iout[Nsamples];
    float Qout[Nsamples];
    int Nreps = 16;
    float IoutS[Nsamples*Nreps];
    float QoutS[Nsamples*Nreps];

    float32_t fmin = 0;
    float32_t fmax = 6000;
    uint32_t Npoints = 256;
    float32_t fstep = (fmax - fmin)/(float32_t)Npoints;
    float32_t gain[Npoints];
    float32_t freq[Npoints];
    float32_t sbs[Npoints];
    float32_t tone[Npoints];
    float32_t image[Npoints];
    TXDecInit();

    uint32_t spectrum_zoom = SPECTRUM_ZOOM_16;
    InitializeFilters(spectrum_zoom, &filters);
    ZoomFFTPrep(spectrum_zoom, &filters);
    DataBlock data;
    data.sampleRate_Hz = 192000;
    data.N = 2048;

    float32_t bandwidth = 192000 / 16;
    float32_t binwidth = bandwidth / 512;
    float32_t fcenter[512];
    float32_t fstart[512];
    float32_t fend[512];
    for (int i=0; i<512; i++){
        fcenter[i] = (float32_t)i*binwidth-bandwidth/2+binwidth/2;
        fstart[i] =  (float32_t)i*binwidth-bandwidth/2;
        fend[i] =    (float32_t)i*binwidth-bandwidth/2+binwidth;
    }
    bands[EEPROMData.currentBand].mode = LSB;
    float32_t amp;
    uint32_t phase = 0;
    for (int i = 0; i<Npoints; i++){
        freq[i] = fmin + (float32_t)i*fstep;

        for (int j=0; j<Nreps; j++){
            phase = CreateIQToneWithPhase(I,Q,2048,sampleRate_Hz,freq[i],phase,0.5);
            end2end(I, Q, Iout, Qout);
            arm_copy_f32(Iout,&IoutS[j*2048],2048);
            arm_copy_f32(Qout,&QoutS[j*2048],2048);
            data.I = Iout;
            data.Q = Qout;
            ZoomFFTExe(&data,spectrum_zoom,&filters);
        }

        if (i==15){
            WriteIQFile(IoutS,QoutS,"TXEndToEnd_IQ_fixed.txt",2048*Nreps);
            WriteFile(psdnew,"TXEndToEnd_psd_fixed.txt",512);
        }
        // Calculate the sideband separation
        // What bin does this tone appear in?
        int j1;
        for (j1=0; j1<512; j1++){
            if ((freq[i]>=fstart[j1]) & (freq[i]<fend[j1])) break;
        }
        int j2 = 256-(j1-256);
        tone[i] = psdnew[j2]; 
        image[i] = psdnew[j1];
        sbs[i] = tone[i]-image[i];
        amp = getmax(Iout,2048);
        gain[i] = amp/0.5;
    }
    WriteIQFile(freq,gain,"TXEndToEnd_passband_fixed.txt",Npoints);
    WriteIQFile(freq,tone,"TXEndToEnd_tone_fixed.txt",Npoints);
    WriteIQFile(freq,image,"TXEndToEnd_image_fixed.txt",Npoints);
    WriteIQFile(freq,sbs,"TXEndToEnd_sidebandseparation_fixed.txt",Npoints);
    EXPECT_EQ(1,1);
}

TEST(TransmitChain, TwoTone){
    uint32_t Nsamples = 2048;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];
    float Iout[Nsamples];
    float Qout[Nsamples];
    int Nreps = 16;
    float IoutS[Nsamples*Nreps];
    float QoutS[Nsamples*Nreps];

    float32_t tone1_Hz = 700.0;
    float32_t tone2_Hz = 1900.0;

    TXDecInit();

    uint32_t phase1 = 0;
    uint32_t phase2 = 0;
    bands[EEPROMData.currentBand].mode = LSB;
    for (int j=0; j<Nreps; j++){
        phase1 = CreateIQToneWithPhase(I,Q,2048,sampleRate_Hz,tone1_Hz,phase1,0.5);
        phase2 = AddIQToneWithPhase(I,Q,2048,sampleRate_Hz,tone2_Hz,phase2,0.5);
        end2end(I, Q, Iout, Qout);
        arm_copy_f32(Iout,&IoutS[j*2048],2048);
        arm_copy_f32(Qout,&QoutS[j*2048],2048);
        // this does not have enough resolution because we're so damned oversampled
        CalcPSD512(&IoutS[j*2048],&QoutS[j*2048]);
    }

    WriteIQFile(IoutS,QoutS,"TXTwoTone_IQ.txt",2048*Nreps);
    WriteFile(psdnew,"TXTwoTone_psd.txt",512);
    EXPECT_EQ(1,1);
}

