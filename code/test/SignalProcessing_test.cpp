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

int CreateIQToneWithPhase(float *I, float *Q, int Nsamples, int sampleRate_Hz,
                int tone_Hz, int phase_index,float amplitude){
    float oneoverfs = 1.0 / (float)sampleRate_Hz;
    for (int i=0;i<Nsamples;i++){
        I[i] = amplitude*cos(-TWO_PI*(float)(i+phase_index)*oneoverfs*tone_Hz);
        Q[i] = amplitude*sin(-TWO_PI*(float)(i+phase_index)*oneoverfs*tone_Hz);
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
    add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    tone2_Hz = 96000-50*96000/512;
    add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    tone2_Hz = 96000-90*96000/512;
    add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    tone2_Hz = 96000-130*96000/512;
    add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    tone2_Hz = 96000-170*96000/512;
    add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    tone2_Hz = 96000-210*96000/512;
    add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
}

TEST(SignalProcessing, EEPROMInitializedCorrectly){
    EXPECT_FLOAT_EQ(GetAmpCorrectionFactor(0),1);
    EXPECT_FLOAT_EQ(GetPhaseCorrectionFactor(0),0);

    EXPECT_FLOAT_EQ(GetAmpCorrectionFactor(6),1);
    EXPECT_FLOAT_EQ(GetPhaseCorrectionFactor(6),0);
}

TEST(SignalProcessing, TestMockRead){
    extern AudioRecordQueue Q_in_L;
    extern AudioRecordQueue Q_in_R;
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    #include "mock_R_data_int.c"
    #include "mock_L_data_int.c"
    for (unsigned i = 0; i < N_BLOCKS; i++) {
        int16_t *L = Q_in_L.readBuffer();
        int16_t *R = Q_in_R.readBuffer();
        Q_in_L.freeBuffer();
        Q_in_R.freeBuffer();
        // Make sure that we've read the values of L_mock and R_mock
        for (size_t k = 0; k < BUFFER_SIZE; k++){
            EXPECT_EQ(L[k],L_mock[i*BUFFER_SIZE+k]);
            EXPECT_EQ(R[k],R_mock[i*BUFFER_SIZE+k]);
        }
    }
}

// Read data into the input buffers
TEST(SignalProcessing, ReadDataIntoBuffers){
    extern AudioRecordQueue Q_in_L;
    extern AudioRecordQueue Q_in_R;
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    DataBlock data;
    float32_t float_buffer_L[2048]; 
    float32_t float_buffer_R[2048]; 
    data.I = float_buffer_L;
    data.Q = float_buffer_R;
    ReadIQInputBuffer(&data);
    #include "mock_R_data_int.c"
    #include "mock_L_data_int.c"
    EXPECT_NEAR(data.I[1],(float)L_mock[1]/32768.0,0.00001);
    EXPECT_NEAR(data.Q[1],(float)R_mock[1]/32768.0,0.00001);
    EXPECT_NEAR(data.I[2047],(float)L_mock[2047]/32768.0,0.00001);
    EXPECT_NEAR(data.Q[2047],(float)R_mock[2047]/32768.0,0.00001);
}

// Reading data into the input buffers returns false when it is empty
/*TEST(SignalProcessing, ReadDataReturnsFalseWhenEmpty){
    extern AudioRecordQueue Q_in_L;
    extern AudioRecordQueue Q_in_R;
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    DataBlock data;
    float32_t float_buffer_L[2048]; 
    float32_t float_buffer_R[2048]; 
    data.I = float_buffer_L;
    data.Q = float_buffer_R;
    error_t val = ReadIQInputBuffer(&data);
    EXPECT_EQ(val,ESUCCESS);
    val = ReadIQInputBuffer(&data);
    EXPECT_EQ(val,ESUCCESS);
    val = ReadIQInputBuffer(&data);
    EXPECT_EQ(val,ESUCCESS);
    val = ReadIQInputBuffer(&data);
    EXPECT_EQ(val,ESUCCESS);
    val = ReadIQInputBuffer(&data);
    EXPECT_EQ(val,EFAIL);
}*/

// Scale the input buffers
TEST(SignalProcessing, ScaleRFData){
    extern AudioRecordQueue Q_in_L;
    extern AudioRecordQueue Q_in_R;
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    DataBlock data;
    float32_t float_buffer_L[2048]; 
    float32_t float_buffer_R[2048]; 
    data.I = float_buffer_L;
    data.Q = float_buffer_R;
    ReadIQInputBuffer(&data);
    float Lpre = data.I[1];
    float Rpre = data.Q[1];
    ApplyRFGain(&data, 3.0,3.0);
    EXPECT_NEAR(data.I[1],Lpre*1.412537545*1.412537545,0.00001);
    EXPECT_NEAR(data.Q[1],Rpre*1.412537545*1.412537545,0.00001);
}

// Is the IQ correction being properly applied? Compare to output of Python code
TEST(SignalProcessing, IQCorrectionCorrect){
    extern AudioRecordQueue Q_in_L;
    extern AudioRecordQueue Q_in_R;
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    DataBlock data;
    float32_t float_buffer_L[2048]; 
    float32_t float_buffer_R[2048]; 
    data.I = float_buffer_L;
    data.Q = float_buffer_R;
    ReadIQInputBuffer(&data);
    ApplyIQCorrection(&data,1.11,-0.09);
    #include "mock_I_data_IQcorrected.c"
    #include "mock_Q_data_IQcorrected.c"
    for (size_t k = 0; k < (BUFFER_SIZE * N_BLOCKS); k++){
        EXPECT_NEAR(data.I[k], I_corrected[k],0.00001);
        EXPECT_NEAR(data.Q[k], Q_corrected[k],0.00001);
    }
}

// Is the FFT calculation correct?
TEST(SignalProcessing, FFTCalculation){
    float I[512]; //  = {+1, 0,-1, 0,+1, 0,-1, 0};
    float Q[512]; // = { 0,-1, 0,+1, 0,-1, 0,+1};
    float buffer_spec_FFT[1024];
    PrepareIQDataFsOver4Tone(I,Q,buffer_spec_FFT);
    
    // perform complex FFT
    arm_cfft_radix2_instance_f32 S;
    arm_cfft_radix2_init_f32(&S, 512, 0, 1);
    EXPECT_EQ(S.fftLen,512);
    arm_cfft_radix2_f32(&S,buffer_spec_FFT);
    
    // Expect the complex part to be zero at every point
    // Expect the real part to be zero at every point except for sample 384, which should be 512
    //   -> the signal contains data at Fs/4, which should be in bin 512/4. But FFT has order 
    //      reversed, to this bin maps to 512/2+512/4 = 256+128 = 384
    //FILE *file = fopen("tmps.txt", "w");
    //fprintf(file, "sample,I,Q\n");
    for (size_t i = 0; i < SPECTRUM_RES; i++) {
        //fprintf(file, "%d,%5.4f,%5.4f\n", i,buffer_spec_FFT[2*i],buffer_spec_FFT[2*i+1]);
        EXPECT_NEAR(buffer_spec_FFT[2*i+1],0,0.0001);
        if (i == (256+128)){
            EXPECT_NEAR(buffer_spec_FFT[2*i],512,0.0001);
        } else {
            EXPECT_NEAR(buffer_spec_FFT[2*i],0,0.0001);
        }
    }
    //fclose(file);
}

// Is the PSD calculation correct?
TEST(SignalProcessing, PSDCalculation){
    float I[512];
    float Q[512];
    PrepareIQDataFsOver4Tone(I,Q,512);
    CalcPSD512(I,Q);

    // Expect the signal to be zero at every point except for sample 128
    //   -> the signal contains data at Fs/4, which is bin 512/4 = 128
    //
    // The PSD values should be log10((I^2+Q^2)*0.7)
    //  => if I=512 & Q=0, this is 5.263637962 IF NO WINDOW FUNCTION IS APPLIED.
    // We apply a Hanning window function. This reduces the amplitude. We then
    // expect the amplitude to be 1/2 at peak (=sum of window), so PSD should be:
    //  = log10(256*256*0.7) = 4.6616

    EXPECT_NEAR(log10f_fast(0.7*512*512),5.263637962,0.0001);
    EXPECT_NEAR(psdnew[128],4.6616,0.001);

}

// Is the frequency translation function working?
TEST(SignalProcessing, FsOver4SampleSwappingCorrect){
    float Re[]  = {+1,+2,+3,+4,+5,+6,+7,+8};
    float Im[]  = {-1,-2,-3,-4,-5,-6,-7,-8};
    float Reo[] = {+1,+2,+3,+4,+5,+6,+7,+8};
    float Imo[] = {-1,-2,-3,-4,-5,-6,-7,-8};
    DataBlock data;
    data.I = Re;
    data.Q = Im;
    data.N = 8;
    FreqShiftFs4(&data);
    // First sample
    EXPECT_FLOAT_EQ(Re[0],Reo[0]);
    EXPECT_FLOAT_EQ(Im[0],Imo[0]);
    EXPECT_FLOAT_EQ(Re[4],Reo[4]);
    EXPECT_FLOAT_EQ(Im[4],Imo[4]);
    // Second sample
    EXPECT_FLOAT_EQ(Re[1],-Imo[1]); 
    EXPECT_FLOAT_EQ(Im[1],Reo[1]);
    EXPECT_FLOAT_EQ(Re[1+4],-Imo[1+4]); 
    EXPECT_FLOAT_EQ(Im[1+4],Reo[1+4]);
    // Third sample
    EXPECT_FLOAT_EQ(Re[2],-Reo[2]); 
    EXPECT_FLOAT_EQ(Im[2],-Imo[2]); 
    EXPECT_FLOAT_EQ(Re[2+4],-Reo[2+4]); 
    EXPECT_FLOAT_EQ(Im[2+4],-Imo[2+4]); 
    // Fourth sample
    EXPECT_FLOAT_EQ(Re[3],Imo[3]); 
    EXPECT_FLOAT_EQ(Im[3],-Reo[3]); 
    EXPECT_FLOAT_EQ(Re[3+4],Imo[3+4]); 
    EXPECT_FLOAT_EQ(Im[3+4],-Reo[3+4]); 
}

// Are the IIR filters needed before the FIR filters?
TEST(SignalProcessing, IIRBeforeFIR){

    // Create the signal
    uint32_t Nsamples = 2048;
    float32_t buf[Nsamples];
    float32_t buf2[Nsamples];
    uint32_t sampleRate_Hz = 192000;
    CreateDoubleTone(buf, Nsamples, sampleRate_Hz, 
                        1000,33000);
    WriteFile(buf, "data_1k_33k.txt", Nsamples);

    FilterConfig receiveFilters;
    uint32_t spectrum_zoom = SPECTRUM_ZOOM_2;
    InitializeFilters(spectrum_zoom, &receiveFilters);
    char fname[50];

    for (int zoom=1; zoom<5; zoom++){
        spectrum_zoom = zoom;
        ZoomFFTPrep(spectrum_zoom, &receiveFilters);
        // 1) IIR filter
        arm_biquad_cascade_df1_f32 (&(receiveFilters.biquadZoomI), buf, buf2, Nsamples);
        sprintf(fname,"data_after_IIR_zoom_%d.txt",spectrum_zoom);
        WriteFile(buf2, fname, Nsamples);

        // 2) then decimate
        //arm_fir_decimate_f32(&(receiveFilters.Fir_Zoom_FFT_Decimate_I), buf2, buf2, Nsamples);
        decimate_f32(buf2, buf2, receiveFilters.zoom_M, Nsamples);
        sprintf(fname,"data_after_IIR_and_decimate_zoom_%d.txt",spectrum_zoom);
        WriteFile(buf2, fname, Nsamples/(1<<spectrum_zoom));

        // Decimate without doing the IIR before
        //arm_fir_decimate_f32(&(receiveFilters.Fir_Zoom_FFT_Decimate_I), buf, buf2, Nsamples);
        decimate_f32(buf, buf2, receiveFilters.zoom_M, Nsamples);
        sprintf(fname,"data_after_decimate_zoom_%d.txt",spectrum_zoom);
        WriteFile(buf2, fname, Nsamples/(1<<spectrum_zoom));
    }
    EXPECT_EQ(1,1);

}

// Is the zoom FFT correct?
TEST(SignalProcessing, ZoomFFTCorrectWhenZoomIs1){
    uint32_t Nsamples = 2048*4;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];
    float tone_Hz = 48000;
    int32_t bin = frequency_to_bin(tone_Hz,512,sampleRate_Hz);
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    WriteIQFile(I,Q,"zoomFFT_IQ.txt",Nsamples);

    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    
    FilterConfig receiveFilters;
    uint32_t spectrum_zoom = SPECTRUM_ZOOM_1;
    InitializeFilters(spectrum_zoom, &receiveFilters);
    ZoomFFTPrep(spectrum_zoom, &receiveFilters);
    ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    // The PSD values should be log10((I^2+Q^2)*0.7)
    //  => if I=256 & Q=0, this is 4.661577971 IF NO WINDOW FUNCTION IS APPLIED.
    // We apply a Hanning window function. This reduces the amplitude. We then
    // expect the amplitude to be 1/2 at peak (=sum of window), so PSD should be:
    //  = log10((0.5*256)^2*0.7) = 4.0595
    EXPECT_NEAR(psdnew[bin],4.0595,0.001);
    WriteFile(psdnew,"zoomFFT_psd.txt",512);

    // Now test a frequency in the lower sideband
    tone_Hz = -48000-20*96000/512; // picked a frequency that is a multiple of the bin width
    bin = frequency_to_bin(tone_Hz,512,sampleRate_Hz);
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    ZoomFFTExe(&data, spectrum_zoom,&receiveFilters);
    EXPECT_NEAR(psdnew[bin],4.0595,0.001);   
}

TEST(SignalProcessing, ZoomFFTCorrectWhenZoomIs1AndShift){
    uint32_t Nsamples = 2048*4;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];
    float tone_Hz = -48000-20*96000/512;

    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    add_comb(I, Q, sampleRate_Hz, Nsamples);
    WriteIQFile(I,Q,"zoomFFT1preshift_IQ.txt",Nsamples);

    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;

    FilterConfig receiveFilters;
    uint32_t spectrum_zoom = SPECTRUM_ZOOM_1;
    InitializeFilters(spectrum_zoom, &receiveFilters);
    ZoomFFTPrep(spectrum_zoom, &receiveFilters);

    FreqShiftFs4(&data);
    WriteIQFile(I,Q,"zoomFFT1postshift_IQ.txt",READ_BUFFER_SIZE);
    data.N = 2048;
    ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    int32_t bin = frequency_to_bin(tone_Hz+48000,512,sampleRate_Hz);
    EXPECT_NEAR(psdnew[bin],4.0595,0.001);
    WriteFile(psdnew,"zoomFFT1_psd.txt",512);
}

// Is the zoom FFT correct when we increase the zoom factor?
TEST(SignalProcessing, ZoomFFTCorrectWhenZoomIs2){
    uint32_t Nsamples = 2048*4;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];

    float tone_Hz;
    tone_Hz = -48000-20*96000/512; // picked a frequency that is a multiple of the bin width
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    add_comb(I, Q, sampleRate_Hz, Nsamples);
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    
    FilterConfig receiveFilters;
    uint32_t spectrum_zoom = SPECTRUM_ZOOM_2;
    InitializeFilters(spectrum_zoom, &receiveFilters);
    ZoomFFTPrep(spectrum_zoom, &receiveFilters);
    WriteIQFile(I,Q,"zoomFFT2preshift_IQ.txt",READ_BUFFER_SIZE);
    FreqShiftFs4(&data);
    WriteIQFile(I,Q,"zoomFFT2postshift_IQ.txt",READ_BUFFER_SIZE);

    // Now we expect the peak to be at -48000-20*96000/512+48000 Hz = -20*96000/512
    int32_t bin = frequency_to_bin(tone_Hz+48000,512,sampleRate_Hz/(1<<spectrum_zoom));
    data.N = 2048;
    ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    WriteFile(psdnew,"zoomFFT2_psd.txt",512);
    EXPECT_NEAR(psdnew[bin],4.0595,0.01);
}

TEST(SignalProcessing, ZoomFFTCorrectWhenZoomIs4){
    uint32_t Nsamples = 2048*4;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];

    float tone_Hz;
    tone_Hz = -48000-20*96000/512; // picked a frequency that is a multiple of the bin width
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    add_comb(I, Q, sampleRate_Hz, Nsamples);
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;

    FilterConfig receiveFilters;
    uint32_t spectrum_zoom = SPECTRUM_ZOOM_4;
    InitializeFilters(spectrum_zoom, &receiveFilters);
    ZoomFFTPrep(spectrum_zoom, &receiveFilters);
    WriteIQFile(I,Q,"zoomFFT4preshift_IQ.txt",READ_BUFFER_SIZE);
    FreqShiftFs4(&data);
    WriteIQFile(I,Q,"zoomFFT4postshift_IQ.txt",READ_BUFFER_SIZE);

    // Now we expect the peak to be at -48000-20*96000/512+48000 Hz = -20*96000/512
    int32_t bin = frequency_to_bin(tone_Hz+48000,512,sampleRate_Hz/(1<<spectrum_zoom));
    data.N = 2048;
    ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    WriteFile(psdnew,"zoomFFT4_psd.txt",512);
    EXPECT_NEAR(psdnew[bin],4.0595,0.01);
}


TEST(SignalProcessing, ZoomFFTCorrectWhenZoomIs8){
    uint32_t Nsamples = 2048*4;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];

    float tone_Hz;
    tone_Hz = -48000-20*96000/512; // picked a frequency that is a multiple of the bin width
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    add_comb(I, Q, sampleRate_Hz, Nsamples);
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;

    FilterConfig receiveFilters;
    uint32_t spectrum_zoom = SPECTRUM_ZOOM_8;
    InitializeFilters(spectrum_zoom, &receiveFilters);
    ZoomFFTPrep(spectrum_zoom, &receiveFilters);
    WriteIQFile(I,Q,"zoomFFT8preshift_IQ.txt",READ_BUFFER_SIZE*4);
    FreqShiftFs4(&data);
    WriteIQFile(I,Q,"zoomFFT8postshift_IQ.txt",READ_BUFFER_SIZE*4);

    // Now we expect the peak to be at -48000-20*96000/512+48000 Hz = -20*96000/512
    int32_t bin = frequency_to_bin(tone_Hz+48000,512,sampleRate_Hz/(1<<spectrum_zoom));
    data.N = 2048;
    bool val = ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    EXPECT_EQ(val,false);
    data.I = &I[READ_BUFFER_SIZE];
    data.Q = &Q[READ_BUFFER_SIZE];
    val = ZoomFFTExe(&data, spectrum_zoom, &receiveFilters);
    WriteFile(psdnew,"zoomFFT8_psd.txt",512);
    EXPECT_NEAR(psdnew[bin],4.0595,0.01);
}

TEST(SignalProcessing, ZoomFFTCorrectWhenZoomIs16){
    uint32_t Nsamples = 2048*4;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];

    float tone_Hz;
    tone_Hz = -48000-20*96000/512; // picked a frequency that is a multiple of the bin width
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);

    float tone2_Hz = tone_Hz + 70.3125*2;
    add_second_tone(I,Q, tone2_Hz, sampleRate_Hz, Nsamples);
    add_comb(I, Q, sampleRate_Hz, Nsamples);
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;

    FilterConfig receiveFilters;
    uint32_t spectrum_zoom = SPECTRUM_ZOOM_16;
    InitializeFilters(spectrum_zoom, &receiveFilters);
    ZoomFFTPrep(spectrum_zoom, &receiveFilters);
    WriteIQFile(I,Q,"zoomFFT16preshift_IQ.txt",READ_BUFFER_SIZE*4);
    FreqShiftFs4(&data);
    WriteIQFile(I,Q,"zoomFFT16postshift_IQ.txt",READ_BUFFER_SIZE*4);

    // Now we expect the peak to be at -48000-20*96000/512+48000 Hz = -20*96000/512
    int32_t bin = frequency_to_bin(tone_Hz+48000,512,sampleRate_Hz/(1<<spectrum_zoom));
    int32_t bin2 = frequency_to_bin(tone2_Hz+48000,512,sampleRate_Hz/(1<<spectrum_zoom));
    
    data.N = 2048;
    bool val = ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    EXPECT_EQ(val,false);
    data.I = &I[READ_BUFFER_SIZE];
    data.Q = &Q[READ_BUFFER_SIZE];
    val = ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    EXPECT_EQ(val,false);
    data.I = &I[2*READ_BUFFER_SIZE];
    data.Q = &Q[2*READ_BUFFER_SIZE];
    val = ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    EXPECT_EQ(val,false);
    data.I = &I[3*READ_BUFFER_SIZE];
    data.Q = &Q[3*READ_BUFFER_SIZE];
    val = ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    EXPECT_EQ(val,true);
    WriteFile(psdnew,"zoomFFT16_psd.txt",512);
    EXPECT_NEAR(psdnew[bin],4.0595,0.01);
    EXPECT_NEAR(psdnew[bin2],4.0595,0.01);
}

TEST(SignalProcessing, FrequencyTranslate){
    uint32_t Nsamples = 2048;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];
    float tone_Hz;
    tone_Hz = 3750;
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    WriteIQFile(I,Q,"frequencyTranslate_pretranslate_IQ.txt",Nsamples);

    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;

    FreqShiftFs4(&data);
    WriteIQFile(I,Q,"frequencyTranslate_midcourse_IQ.txt",Nsamples);
    EEPROMData.fineTuneFreq_Hz = 48000+3000;
    float32_t shift = -EEPROMData.fineTuneFreq_Hz;
    FreqShiftF(&data,shift);

    WriteIQFile(I,Q,"frequencyTranslate_posttranslate_IQ.txt",Nsamples);
    // Detect it at frequency 2
    int32_t bin = frequency_to_bin(750,512,sampleRate_Hz);

    FilterConfig receiveFilters;
    uint32_t spectrum_zoom = SPECTRUM_ZOOM_1;
    InitializeFilters(spectrum_zoom, &receiveFilters);
    ZoomFFTPrep(spectrum_zoom, &receiveFilters);
    ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    EXPECT_NEAR(psdnew[bin],4.0595,0.003);
}



TEST(SignalProcessing, FineTuneTranslate){
    // Make a tone at frequency 1
    uint32_t Nsamples = 2048;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];
    float tone_Hz;
    tone_Hz = -48000-20*96000/512;
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    WriteIQFile(I,Q,"fineTuneTranslate_pretranslate_IQ.txt",Nsamples);

    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;

    // Translate it by x
    float offset_Hz = 40*96000/512;
    FreqShiftF(&data, offset_Hz);
    WriteIQFile(I,Q,"fineTuneTranslate_posttranslate_IQ.txt",Nsamples);
    // Detect it at frequency 2
    int32_t bin = frequency_to_bin(tone_Hz+offset_Hz,512,sampleRate_Hz);

    FilterConfig receiveFilters;
    uint32_t spectrum_zoom = SPECTRUM_ZOOM_1;
    InitializeFilters(spectrum_zoom, &receiveFilters);
    ZoomFFTPrep(spectrum_zoom, &receiveFilters);
    ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    EXPECT_NEAR(psdnew[bin],4.0595,0.001);
}

TEST(SignalProcessing, FineTunePhaseDiscontinuity){
    // Make a tone at frequency 1
    uint32_t Nsamples = 2048*4;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];
    float tone_Hz;
    tone_Hz = -48000-1*96000/512;
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);

    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;

    FreqShiftFs4(&data);
    WriteIQFile(I,Q,"fineTunePhase_pretranslate_IQ.txt",Nsamples);

    // Translate it by x
    float offset_Hz = +2200.5;
    data.N = 2048;
    for (uint16_t i=0; i<4; i++){
        data.I = &I[2048*i];
        data.Q = &Q[2048*i];
        FreqShiftF(&data,offset_Hz);
    }
    //FreqShiftF(&I[2048*0],&Q[2048*0],2048,offset_Hz,sampleRate_Hz);
    //FreqShiftF(&I[2048*1],&Q[2048*1],2048,offset_Hz,sampleRate_Hz);
    //FreqShiftF(&I[2048*2],&Q[2048*2],2048,offset_Hz,sampleRate_Hz);
    //FreqShiftF(&I[2048*3],&Q[2048*3],2048,offset_Hz,sampleRate_Hz);
    WriteIQFile(I,Q,"fineTunePhase_posttranslate_IQ.txt",Nsamples);
    // Peak will now be at this bin
    int32_t bin = frequency_to_bin(tone_Hz+offset_Hz+48000,512,sampleRate_Hz);

    // Phase discontinuities become clear when the FFT zoom is 8 or greater
    // If there are phase discontinuities, there is a lot more power away
    // from the peak. So we set a threshold (determined experimentally) of 
    // 80 dB between the peak and the floor
    uint32_t spectrum_zoom = SPECTRUM_ZOOM_8;
    FilterConfig receiveFilters;
    InitializeFilters(spectrum_zoom, &receiveFilters);
    ZoomFFTPrep(spectrum_zoom, &receiveFilters);

    data.I = &I[2048*0];
    data.Q = &Q[2048*0];
    ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    data.I = &I[2048*1];
    data.Q = &Q[2048*1];
    ZoomFFTExe(&data,spectrum_zoom,&receiveFilters);
    WriteFile(psdnew,"fineTunePhase_psd.txt",512);
    EXPECT_LT(psdnew[bin-50],4.0595-8.0); // expect noise to be 80 dB below peak
}

TEST(SignalProcessing, FineTuneProcessingTime){
    // Make a tone at frequency 1
    uint32_t Nsamples = 2048;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];
    float tone_Hz;
    tone_Hz = -48000-20*96000/512;
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;

    // Translate it by x
    float offset_Hz = 40*96000/512;

    FILE *file = fopen("FineTuneTime.txt", "w");
    struct timeval tv;
    gettimeofday(&tv,NULL);
    unsigned long before_us = 1000000 * tv.tv_sec + tv.tv_usec;
    for (size_t i =0; i<1; i++){
        FreqShiftF(&data,offset_Hz);
    }
    gettimeofday(&tv,NULL);
    unsigned long after_us = 1000000 * tv.tv_sec + tv.tv_usec;
    fprintf(file, "FreqShiftF: %ld us\n", after_us-before_us);

    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    gettimeofday(&tv,NULL);
    before_us = 1000000 * tv.tv_sec + tv.tv_usec;
    for (size_t i =0; i<1; i++){
        FreqShiftF2(I,Q,Nsamples,offset_Hz,sampleRate_Hz);
    }
    gettimeofday(&tv,NULL);
    after_us = 1000000 * tv.tv_sec + tv.tv_usec;
    fprintf(file, "FreqShiftF2: %ld us\n", after_us-before_us);

    fclose(file);
}

TEST(SignalProcessing, DecimateBy4){
    // For all the decimate by N tests, run two blocks through because the state
    // vector for the FIR filter starts at zero for the first block, which introduces
    // artifacts
    uint32_t Nsamples = 2048*2;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];
    float tone_Hz;
    tone_Hz = -10*96000/512;
    ResetPSD();
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    WriteIQFile(I,Q,"DecimateBy4_original_IQ.txt",Nsamples);

    FilterConfig filters;
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);

    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = 2048;
    data.sampleRate_Hz = sampleRate_Hz;

    DecimateBy4(&data,&filters);
    data.I = &I[2048];
    data.Q = &Q[2048];
    data.N = 2048;
    DecimateBy4(&data,&filters);
    WriteIQFile(&I[2048],&Q[2048],"DecimateBy4_decimated_IQ.txt",Nsamples/4/2);

    CalcPSD512(&I[2048],&Q[2048]);
    int32_t bin = frequency_to_bin(tone_Hz,512,sampleRate_Hz/4);
    WriteFile(psdnew,"DecimateBy4_psd.txt",512);

    float32_t peak_to_floor = psdnew[bin] - psdnew[bin+10];
    EXPECT_GT(peak_to_floor,10);
}

TEST(SignalProcessing, DecimateBy2){
    // For all the decimate by N tests, run two blocks through because the state
    // vector for the FIR filter starts at zero for the first block, which introduces
    // artifacts
    uint32_t Nsamples = 512*2;
    uint32_t sampleRate_Hz = 192000/4;
    float I[Nsamples];
    float Q[Nsamples];
    float tone_Hz;
    tone_Hz = -10*96000/512;
    ResetPSD();
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    WriteIQFile(I,Q,"DecimateBy2_original_IQ.txt",Nsamples);

    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = 512;
    data.sampleRate_Hz = sampleRate_Hz;

    FilterConfig filters;
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);

    DecimateBy2(&data,&filters);
    //WriteIQFile(I,Q,"DecimateBy2_decimated_IQ.txt",Nsamples/4);
    data.I = &I[512];
    data.Q = &Q[512];
    data.N = 512;
    DecimateBy2(&data,&filters);
    WriteIQFile(&I[512],&Q[512],"DecimateBy2_decimated_IQ.txt",Nsamples/2/2);

    CalcPSD256(&I[512],&Q[512]);
    int32_t bin = frequency_to_bin(tone_Hz,256,sampleRate_Hz/2);
    WriteFile(psdnew,"DecimateBy2_psd.txt",256);

    float32_t peak_to_floor = psdnew[bin] - psdnew[bin+10];
    EXPECT_GT(peak_to_floor,2.7);
}

TEST(SignalProcessing, DecimateBy8){
    // For all the decimate by N tests, run two blocks through because the state
    // vector for the FIR filter starts at zero for the first block, which introduces
    // artifacts
    uint32_t Nsamples = 2048*2;
    uint32_t sampleRate_Hz = 192000;
    float I[Nsamples];
    float Q[Nsamples];
    float tone_Hz;
    tone_Hz = -10*96000/512;
    ResetPSD();
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, tone_Hz);
    WriteIQFile(I,Q,"DecimateBy8_original_IQ.txt",Nsamples);

    FilterConfig filters;
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);

    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = 2048;
    data.sampleRate_Hz = sampleRate_Hz;
    DecimateBy8(&data,&filters);
    data.I = &I[2048];
    data.Q = &Q[2048];
    data.N = 2048;
    DecimateBy8(&data,&filters);
    WriteIQFile(&I[2048],&Q[2048],"DecimateBy8_decimated_IQ.txt",Nsamples/8/2);

    CalcPSD256(I,Q);
    int32_t bin = frequency_to_bin(tone_Hz,256,sampleRate_Hz/8);
    WriteFile(psdnew,"DecimateBy8_psd.txt",256);

    float32_t peak_to_floor = psdnew[bin] - psdnew[bin+10];
    EXPECT_GT(peak_to_floor,2.7);
}


TEST(SignalProcessing, InitFIRFilterMask){
    extern FilterConfig filters;
    float32_t DMAMEM FIR_filter_mask[FFT_LENGTH * 2] __attribute__((aligned(4)));
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    InitFilterMask(FIR_filter_mask, &filters);
    WriteFile(FIR_filter_mask,"FIR_filter_mask.txt",1024);
    // Between frequency bins 460 and 500 we expect the magnitude of the
    // complex frequency mask to be 1
    for (int i=460; i<500; i++){
        float32_t mag = sqrt(FIR_filter_mask[i*2]*FIR_filter_mask[i*2] + 
            FIR_filter_mask[i*2+1]*FIR_filter_mask[i*2+1]);
        EXPECT_NEAR(mag,1.0,0.001);
    }

    // Between frequency bins 10 and 400 we expect the magnitude of the
    // complex frequency mask to be 0
    for (int i=10; i<400; i++){
        float32_t mag = sqrt(FIR_filter_mask[i*2]*FIR_filter_mask[i*2] + 
            FIR_filter_mask[i*2+1]*FIR_filter_mask[i*2+1]);
        EXPECT_NEAR(mag,0.0,0.001);
    }
}

TEST(SignalProcessing, ConvolutionFilter){
    // we expect signals between -200 and -3000 Hz to pass through, others to be blocked
    extern FilterConfig filters;
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    uint32_t Nsamples = 512+256; // 2048/8;
    uint32_t sampleRate_Hz = 192000/8;
    float I[Nsamples];
    float Q[Nsamples];

    float Iout[512];
    float Qout[512];

    // bin width is sample rate / number of bins
    // 192000/8/512 = 46.875
    float tone_Hz = 192000/8/512*10; // 468.75 Hz
    ResetPSD();
    for (int i=0; i<Nsamples; i++){
        I[i] = 0;
        Q[i] = 0;
    }
    // add comb of tones between -4687.5 and +4687.5
    for (int i=1; i<11; i++){
        add_second_tone(I, Q, -i*tone_Hz+192000/8/512/2, sampleRate_Hz, Nsamples);
        add_second_tone(I, Q, +i*tone_Hz+192000/8/512/2, sampleRate_Hz, Nsamples);
    }
    //CreateIQChirp(I,Q,Nsamples,sampleRate_Hz);

    WriteIQFile(I,Q,"ConvolutionFilter_original_IQ.txt",512);

    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = 256;
    data.sampleRate_Hz = sampleRate_Hz;
    ConvolutionFilter(&data, &filters,"ConvolutionFilter_pass1.txt");
    WriteIQFile(data.I,data.Q,"ConvolutionFilter_pass1_filtered_IQ.txt",256);

    data.I = &I[256];
    data.Q = &Q[256];
    ConvolutionFilter(&data, &filters, "ConvolutionFilter_pass2.txt");
    WriteIQFile(data.I,data.Q,"ConvolutionFilter_pass2_filtered_IQ.txt",256);
    for (unsigned i = 0; i < 256; i++) {
        Iout[i] = data.I[i];
        Qout[i] = data.Q[i];
    }

    data.I = &I[256*2];
    data.Q = &Q[256*2];
    ConvolutionFilter(&data, &filters, "ConvolutionFilter_pass3.txt");
    WriteIQFile(data.I,data.Q,"ConvolutionFilter_pass3_filtered_IQ.txt",256);
    for (unsigned i = 0; i < 256; i++) {
        Iout[256+i] = data.I[i];
        Qout[256+i] = data.Q[i];
    }
    CalcPSD512(Iout,Qout);
    WriteFile(psdnew,"ConvolutionFilter_filtered_PSD.txt",512);

    // Check that the signals were passed and/or attenuated as expected 
    int32_t bin0 = frequency_to_bin(-tone_Hz,512,sampleRate_Hz);
    EXPECT_EQ(bin0,247);
    EXPECT_NEAR(psdnew[bin0],4.0595,0.07); // allow roughly 0.7dB passband attenuation
    // passband points
    EXPECT_NEAR(psdnew[bin0-10],psdnew[bin0],0.19); // allow 1.9 dB of passband ripple
    EXPECT_NEAR(psdnew[bin0-20],psdnew[bin0],0.19);
    EXPECT_NEAR(psdnew[bin0-30],psdnew[bin0],0.19);
    EXPECT_NEAR(psdnew[bin0-40],psdnew[bin0],0.19);
    // stopband points
    EXPECT_GT(psdnew[bin0] - psdnew[bin0-80],10); // 100 dB of stopband attenuation
    EXPECT_GT(psdnew[bin0] - psdnew[bin0-90],10); // 100 dB of stopband attenuation
}


TEST(SignalProcessing, ConvolutionFilterChanges){
    // What does the passband look like if we change the filter limits?
    extern FilterConfig filters;
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    uint32_t Nsamples = 512+256; // 2048/8;
    uint32_t sampleRate_Hz = 192000/8;
    float I[Nsamples];
    float Q[Nsamples];

    float Iout[512];
    float Qout[512];

    // bin width is sample rate / number of bins
    // 192000/8/512 = 46.875
    float tone_Hz = 192000/8/512*10; // 468.75 Hz
    ResetPSD();
    for (int i=0; i<Nsamples; i++){
        I[i] = 0;
        Q[i] = 0;
    }
    // add comb of tones between -4687.5 and +4687.5
    for (int i=1; i<11; i++){
        add_second_tone(I, Q, -i*tone_Hz+192000/8/512/2, sampleRate_Hz, Nsamples);
        add_second_tone(I, Q, +i*tone_Hz+192000/8/512/2, sampleRate_Hz, Nsamples);
    }
    //CreateIQChirp(I,Q,Nsamples,sampleRate_Hz);

    WriteIQFile(I,Q,"ConvolutionFilterChange_original_IQ.txt",512);

    // Change the band limits
    bands[EEPROMData.currentBand].FLoCut_Hz = -2000;
    bands[EEPROMData.currentBand].FHiCut_Hz = -1000;
    UpdateFIRFilterMask(&filters);

    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = 256;
    data.sampleRate_Hz = sampleRate_Hz;
    ConvolutionFilter(&data, &filters,"ConvolutionFilterChange_pass1.txt");
    WriteIQFile(data.I,data.Q,"ConvolutionFilterChange_pass1_filtered_IQ.txt",256);

    data.I = &I[256];
    data.Q = &Q[256];
    ConvolutionFilter(&data, &filters, "ConvolutionFilterChange_pass2.txt");
    WriteIQFile(data.I,data.Q,"ConvolutionFilterChange_pass2_filtered_IQ.txt",256);
    for (unsigned i = 0; i < 256; i++) {
        Iout[i] = data.I[i];
        Qout[i] = data.Q[i];
    }

    data.I = &I[256*2];
    data.Q = &Q[256*2];
    ConvolutionFilter(&data, &filters, "ConvolutionFilterChange_pass3.txt");
    WriteIQFile(data.I,data.Q,"ConvolutionFilterChange_pass3_filtered_IQ.txt",256);
    for (unsigned i = 0; i < 256; i++) {
        Iout[256+i] = data.I[i];
        Qout[256+i] = data.Q[i];
    }
    CalcPSD512(Iout,Qout);
    WriteFile(psdnew,"ConvolutionFilterChange_filtered_PSD.txt",512);

    // Analyse using analyze_filter_chain.ipynb to confirm change happened as expected
}

TEST(SignalProcessing, AGCInitializesCorrectly){
    EEPROMData.agc = AGCLong;
    EXPECT_FLOAT_EQ(agc.hangtime,0.25);
    InitializeAGC(&agc,SR[SampleRate].rate);
    EXPECT_FLOAT_EQ(agc.hangtime,2.0);
}

TEST(SignalProcessing, AGCOffMultipliesByConstant){
    InitializeAGC(&agc,SR[SampleRate].rate);
    EEPROMData.agc = AGCOff;
    
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    for (int i=0; i<Nsamples; i++){
        I[i] = 1;
        Q[i] = 1;
    }
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = SR[SampleRate].rate;

    AGC(&data, &agc);
    EXPECT_FLOAT_EQ(data.I[0],agc.fixed_gain);
    EXPECT_FLOAT_EQ(data.I[Nsamples/2],agc.fixed_gain);
    EXPECT_FLOAT_EQ(data.I[Nsamples-1],agc.fixed_gain);
    EXPECT_FLOAT_EQ(data.Q[0],agc.fixed_gain);
    EXPECT_FLOAT_EQ(data.Q[Nsamples/2],agc.fixed_gain);
    EXPECT_FLOAT_EQ(data.Q[Nsamples-1],agc.fixed_gain);
}

TEST(SignalProcessing, AGCRecoveryTime){
    // Establish a tone at one amplitude for one second, then 10x the tone
    // amplitude for 100ms before returning to the original level for several
    // seconds. Measure the recovery time of the AGC loop gains.

    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t toneFreq_Hz = -440.0;
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;
    
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;

    // SR[SampleRate].rate/filters.DF / Nsamples = 94 --> make it 100 
    uint16_t Nreps1 = 100; // ~ 1 second
    uint16_t Nreps2 = 10;  // ~ 0.1 seconds
    uint16_t Nreps3 = 300; // ~ 3 seconds
    float32_t Imaxes[Nreps1+Nreps2+Nreps3];
    float32_t Qmaxes[Nreps1+Nreps2+Nreps3];
    int phase = 0;

    for (int i=0; i<5; i++){
        
        switch (i){
            case 0:
                EEPROMData.agc = AGCOff;
                break;
            case 1:
                EEPROMData.agc = AGCLong;
                break;
            case 2:
                EEPROMData.agc = AGCSlow;
                break;
            case 3:
                EEPROMData.agc = AGCMed;
                break;
            case 4:
                EEPROMData.agc = AGCFast;
                break;
        }
        InitializeAGC(&agc,SR[SampleRate].rate/filters.DF);

        char strbuf[50];
        for (size_t i = 0; i < Nreps1; i++){
            phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.01);
            AGC(&data, &agc);
            Imaxes[i] = get_max(data.I,data.N);
        }
        for (size_t i = 0; i < Nreps2; i++){
            phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.5);
            AGC(&data, &agc);
            Imaxes[Nreps1+i] = get_max(data.I,data.N);
        }
        for (size_t i = 0; i < Nreps3; i++){
            phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.01);
            AGC(&data, &agc);
            Imaxes[Nreps1+Nreps2+i] = get_max(data.I,data.N);
        }

        sprintf(strbuf,"AGC%d_Imagnitudes.txt",i);
        WriteFile(Imaxes,strbuf,Nreps1+Nreps2+Nreps3);

        // If AGC is engaged, the max magnitude is pegged to this level, determined experimentally
        if (i > 0){
            EXPECT_NEAR(Imaxes[Nreps1+5],0.898494,0.001);
        }
        // The setting determines how long it takes the gain to recover to pre-spike levels
        switch (EEPROMData.agc){
            case AGCOff:
                // AGCOff, no recovery time
                EXPECT_NEAR(Imaxes[Nreps1+Nreps2+1],Imaxes[50],0.001);
                break;
            case AGCLong:
                // AGCLong starts recovering after 195 sample blocks and has 
                // fully recovered by 210 sample blocks
                EXPECT_NEAR(Imaxes[Nreps1+Nreps2+210],Imaxes[50],0.001);
                EXPECT_LE(Imaxes[Nreps1+Nreps2+210-20],Imaxes[50]/2);
                break;
            case AGCSlow:
                // AGCSlow starts recovering after 90 sample blocks and has 
                // fully recovered by 115 sample blocks
                EXPECT_NEAR(Imaxes[Nreps1+Nreps2+115],Imaxes[50],0.001);
                EXPECT_LE(Imaxes[Nreps1+Nreps2+115-25],Imaxes[50]/2);
                break;
            case AGCMed:
                // AGCMed starts recovering immediately and has 
                // fully recovered by 20 sample blocks
                EXPECT_NEAR(Imaxes[Nreps1+Nreps2+20],Imaxes[50],0.001);
                EXPECT_LE(Imaxes[Nreps1+Nreps2+5],Imaxes[50]/2);
                break;
            case AGCFast:
                // AGCFast starts recovering immediately but takes a long time 
                // and fully recovered by 210 sample blocks
                EXPECT_NEAR(Imaxes[Nreps1+Nreps2+210],Imaxes[50],0.001);
                EXPECT_LE(Imaxes[Nreps1+Nreps2+210-100],Imaxes[50]/2);
                break;
        }
    }
}

TEST(SignalProcessing, DemodulateLSB){
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t toneFreq_Hz = -440.0;
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;
    
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz);
    
    bands[EEPROMData.currentBand].mode = LSB;
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    float32_t preI = data.I[Nsamples/2];
    Demodulate(&data, &filters);

    // LSB and USB just copy I into Q
    EXPECT_FLOAT_EQ( data.I[0], data.Q[0] );
    EXPECT_FLOAT_EQ( data.I[Nsamples/2], preI );
    EXPECT_FLOAT_EQ( data.I[Nsamples/2], data.Q[Nsamples/2] );
    EXPECT_FLOAT_EQ( data.I[Nsamples-1], data.Q[Nsamples-1] );

}

TEST(SignalProcessing, DemodulateUSB){
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t toneFreq_Hz = -440.0;
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;
    
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    CreateIQTone(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz);
    
    bands[EEPROMData.currentBand].mode = USB;
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    float32_t preI = data.I[Nsamples/2];
    Demodulate(&data, &filters);

    // LSB and USB just copy I into Q
    EXPECT_FLOAT_EQ( data.I[0], data.Q[0] );
    EXPECT_FLOAT_EQ( data.I[Nsamples/2], preI );
    EXPECT_FLOAT_EQ( data.I[Nsamples/2], data.Q[Nsamples/2] );
    EXPECT_FLOAT_EQ( data.I[Nsamples-1], data.Q[Nsamples-1] );
}

void AM_IIR_filter_tone(float32_t toneFreq_Hz, DataBlock *dout, float32_t *gain){
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    DataBlock data;
    dout = &data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    uint32_t phase = 0;
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    arm_biquad_cascade_df1_f32(&filters.biquadAudioLowPass, data.I, data.Q, data.N);
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    arm_biquad_cascade_df1_f32(&filters.biquadAudioLowPass, data.I, data.Q, data.N);
    arm_copy_f32(data.Q, data.I, data.N);
    // Find the max value in data.I
    float32_t amp = 0.0;
    for (size_t i=0; i<data.N; i++){
        if (data.I[i] > amp) amp = data.I[i];
    }
    *gain = amp/0.1;
}

TEST(SignalProcessing, AudioIIRFilterCorrect){
    // Measure the passband of the IIR filter
    DataBlock *dout;
    float32_t fmin = 50.0;
    float32_t fmax = 12000.0;
    uint32_t Npoints = 101;
    float32_t fstep = (fmax - fmin)/(float32_t)Npoints;
    float32_t gain[Npoints];
    float32_t freq[Npoints];   
    for (int i = 0; i<Npoints; i++){
        freq[i] = fmin + (float32_t)i*fstep;
        AM_IIR_filter_tone(freq[i], dout, &gain[i]);
    }
    WriteIQFile(freq,gain,"AudioIIRPassband.txt",Npoints);
    EXPECT_NEAR(gain[0],1,0.001); // gain at fmin should be close to 1
    EXPECT_LT(gain[Npoints-1],1e-4); // gain at fmax should be less than 1e-4
    
}

TEST(SignalProcessing, DemodulateAM){
    // Generate an AM modulated signal as it appears at IF frequencies.
    // In the simplest case, I is offset from zero and has some amplitude. Q is all zeros.
    
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t toneFreq_Hz = 440.0;
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;
    
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    uint32_t phase = 0;

    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    bands[EEPROMData.currentBand].mode = AM;
    char strbuf[50];
    for (size_t i = 0; i<3; i++){
        phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.5);
        for (size_t j = 0; j<data.N; j++){
            I[j] = I[j]/2 + 0.5; // sinusoid with peak to peak amp of 0.5V centered on 0.5V
            Q[j] = 0.0; 
        }
        sprintf(strbuf,"DemodAM_IQ_pass%d.txt",i+1);
        WriteIQFile(I,Q,strbuf,Nsamples);
        // Demodulate it
        Demodulate(&data, &filters);
        sprintf(strbuf,"DemodAM_demodded_pass%d.txt",i+1);
        WriteIQFile(I,Q,strbuf,Nsamples);
    }     

    // Does it match the expected result?
    EXPECT_EQ(1,0);
}

TEST(SignalProcessing, DemodulateSAM){
    EXPECT_EQ(1,0);
}


void EQ_filter_tone(float32_t toneFreq_Hz, uint16_t bf, DataBlock *dout, float32_t *gain){
   
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;
   
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    memset(filters.eqSumBuffer, 0, sizeof(float32_t)*Nsamples);
    DataBlock data;
    dout = &data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    uint32_t phase = 0;
    // Need four iterations for the filter to "warm up" -- i.e., for the IIR filter state vector
    // to reach equilibrium -- at the lowest bands
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    ApplyEQBandFilter(&data, &filters, bf, RX);
    
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    memset(filters.eqSumBuffer, 0, sizeof(float32_t)*data.N);
    ApplyEQBandFilter(&data, &filters, bf, RX);
    
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    memset(filters.eqSumBuffer, 0, sizeof(float32_t)*data.N);
    ApplyEQBandFilter(&data, &filters, bf, RX);

    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    memset(filters.eqSumBuffer, 0, sizeof(float32_t)*data.N);
    ApplyEQBandFilter(&data, &filters, bf, RX);
    
    // Find the max value in data.I
    float32_t amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (filters.eqSumBuffer[i] > amp) amp = filters.eqSumBuffer[i];
    }
    *gain = amp/0.1;
}

/*TEST(SignalProcessing, ReceiveEQPlotPassbands){
    // Generate a bunch of tones between fmin and fmax. Measure the output of
    // the filtered data for each tone. Measure the passband in this way.

    DataBlock *dout;
    float32_t fmin = 100.0;
    float32_t fmax = 8000.0;
    uint32_t Npoints = 401;
    float32_t fstep = (fmax - fmin)/(float32_t)Npoints;
    float32_t gain[Npoints];
    float32_t freq[Npoints];
    char strbuf[50];

    for (uint16_t bf = 0; bf < 14; bf++){
        for (int i = 0; i<Npoints; i++){
            freq[i] = fmin + (float32_t)i*fstep;
            EQ_filter_tone(freq[i], bf, dout, &gain[i]);
        }
        sprintf(strbuf,"ReceiveEQ_band_%d.txt",bf);
        WriteIQFile(freq,gain,strbuf,Npoints);
    }
}*/

TEST(SignalProcessing, ReceiveEQPassbands){
    DataBlock *dout;
    // Do a numerical test at center of passband and at 2*center
    float32_t band_center_Hz[] = {198.425,250,314.98,400,500,630,793,1000,1259,1587,2000,2500,3150,4000};
    float32_t gn;
    for (uint16_t bf = 0; bf < 14; bf++){
        EQ_filter_tone(band_center_Hz[bf], bf, dout, &gn);
        EXPECT_GT(10*log10f(gn),-0.01); // passband should be > -0.01 dB
        EQ_filter_tone(band_center_Hz[bf]*2, bf, dout, &gn);
        EXPECT_LT(10*log10f(gn),-20); // transmission should be smaller than -20dB at 2*fc
    }
}

TEST(SignalProcessing, ReceiveEQFiltersCorrectly){

    float32_t band_low_Hz = 198.425 / 2; // expect at least 20 dB of attenuation
    float32_t band_high_Hz = 4000*2; // expect at least 20dB of attenuation
    float32_t band_mid_Hz = 800.0; // expect good transmission
    
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float Itn1[Nsamples];
    float Qtn1[Nsamples];
    float Itn2[Nsamples];
    float Qtn2[Nsamples];
    float Itn3[Nsamples];
    float Qtn3[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;   
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    uint32_t phase1 = 0;
    uint32_t phase2 = 0;
    uint32_t phase3 = 0;
    // Need four iterations for the filter to "warm up" -- i.e., for the IIR filter state vector
    // to reach equilibrium -- at the lowest bands
    for (size_t i=0; i<4; i++){
        phase1 = CreateIQToneWithPhase(Itn1, Qtn1, Nsamples, sampleRate_Hz, band_low_Hz, phase1, 0.1);
        data.I = Itn1;
        data.Q = Qtn1;
        BandEQ(&data, &filters, RX);
    }
    // Find the max value in data.I
    float32_t amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.I[i] > amp) amp = data.I[i];
    }
    EXPECT_LT(10*log10f(amp/0.1),-20); // transmission should be smaller than -20dB

    for (size_t i=0; i<4; i++){
        phase2 = CreateIQToneWithPhase(Itn2, Qtn2, Nsamples, sampleRate_Hz, band_mid_Hz, phase2, 0.1);
        data.I = Itn2;
        data.Q = Qtn2;
        BandEQ(&data, &filters, RX);
    }
    amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.I[i] > amp) amp = data.I[i];
    }
    EXPECT_GT(10*log10f(amp/0.1),-0.1); // insertion loss should be no more than -0.1 dB

    for (size_t i=0; i<4; i++){
        phase3 = CreateIQToneWithPhase(Itn3, Qtn3, Nsamples, sampleRate_Hz, band_high_Hz, phase3, 0.1);
        data.I = Itn3;
        data.Q = Qtn3;
        BandEQ(&data, &filters, RX);
    }
    amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.I[i] > amp) amp = data.I[i];
    }
    EXPECT_LT(10*log10f(amp/0.1),-20); // transmission should be smaller than -20dB
}

TEST(SignalProcessing, Kim1NR){
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;   
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    InitializeKim1NoiseReduction();
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    uint32_t phase = 0;
    char strbuf[50];
    for (int k=0; k<7; k++){
        phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, 440.0, phase, 0.1);
        sprintf(strbuf, "KimNR_pre%d.txt",k+1);
        WriteIQFile(I,Q,strbuf,Nsamples);
        Kim1_NR(&data);
        sprintf(strbuf, "KimNR_post%d.txt",k+1);
        WriteIQFile(I,Q,strbuf,Nsamples);
    }
    // Have not researched the algorithm to be able to write a sensible test. Just test to 
    // make sure that the output is something.
    float32_t amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.I[i] > amp) amp = data.I[i];
    }
    EXPECT_GT(amp,0.1/2);
}

TEST(SignalProcessing, XanrNoise){
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;   
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    InitializeXanrNoiseReduction();
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    uint32_t phase = 0;
    char strbuf[50];
    for (int k=0; k<7; k++){
        phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, 440.0, phase, 0.1);
        sprintf(strbuf, "XanrNR_pre%d.txt",k+1);
        WriteIQFile(I,Q,strbuf,Nsamples);
        Xanr(&data,0);
        sprintf(strbuf, "XanrNR_post%d.txt",k+1);
        WriteIQFile(I,Q,strbuf,Nsamples);
    }
    // Have not researched the algorithm to be able to write a sensible test. Just test to 
    // make sure that the output is something.
    float32_t amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.Q[i] > amp) amp = data.Q[i];
    }
    EXPECT_GT(amp,0.008);
}

TEST(SignalProcessing, XanrNotch){
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;   
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    InitializeXanrNoiseReduction();
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    uint32_t phase = 0;
    char strbuf[50];
    for (int k=0; k<7; k++){
        phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, 440.0, phase, 0.1);
        sprintf(strbuf, "XanrNotch_pre%d.txt",k+1);
        WriteIQFile(I,Q,strbuf,Nsamples);
        Xanr(&data,1);
        sprintf(strbuf, "XanrNotch_post%d.txt",k+1);
        WriteIQFile(I,Q,strbuf,Nsamples);
    }
    // Have not researched the algorithm to be able to write a sensible test. Just test to 
    // make sure that the output is something.
    float32_t amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.Q[i] > amp) amp = data.Q[i];
    }
    EXPECT_GT(amp,0.09);
}

TEST(SignalProcessing, SpectralNoiseReduction){
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;   
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    InitializeSpectralNoiseReduction();
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    uint32_t phase = 0;
    char strbuf[50];
    for (int k=0; k<7; k++){
        phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, 440.0, phase, 0.1);
        sprintf(strbuf, "SpectralNR_pre%d.txt",k+1);
        WriteIQFile(I,Q,strbuf,Nsamples);
        SpectralNoiseReduction(&data);
        sprintf(strbuf, "SpectralNR_post%d.txt",k+1);
        WriteIQFile(I,Q,strbuf,Nsamples);
    }
    // Have not researched the algorithm to be able to write a sensible test. Just test to 
    // make sure that the output is something.
    float32_t amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.I[i] > amp) amp = data.I[i];
    }
    EXPECT_GT(amp,0.09);
}

TEST(SignalProcessing, NoiseReduction){
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;   
    EEPROMData.spectrum_zoom = SPECTRUM_ZOOM_1;
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    InitializeSignalProcessing();
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    uint32_t phase = 0;
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, 440.0, phase, 0.1);
    
    EEPROMData.nrOptionSelect = NROff;
    float32_t Ipre = data.I[0];
    NoiseReduction(&data);
    EXPECT_FLOAT_EQ(Ipre,data.I[0]);
    
    EEPROMData.nrOptionSelect = NRKim;
    WriteIQFile(I,Q,"NR_preK.txt",Nsamples);
    NoiseReduction(&data);
    WriteIQFile(I,Q,"NR_postK.txt",Nsamples);
    float32_t amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.I[i] > amp) amp = data.I[i];
    }
    EXPECT_GT(amp,0.4);

    EEPROMData.nrOptionSelect = NRSpectral;
    phase = 0;
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, 440.0, phase, 0.1);
    WriteIQFile(I,Q,"NR_preS.txt",Nsamples);
    NoiseReduction(&data);
    WriteIQFile(I,Q,"NR_postS.txt",Nsamples);
    amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.I[i] > amp) amp = data.I[i];
    }
    EXPECT_GT(amp,0.09);
    
    EEPROMData.nrOptionSelect = NRLMS;
    phase = 0;
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, 440.0, phase, 0.1);
    WriteIQFile(I,Q,"NR_preL.txt",Nsamples);
    NoiseReduction(&data);
    WriteIQFile(I,Q,"NR_postL.txt",Nsamples);
    amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.I[i] > amp) amp = data.I[i];
    }
    EXPECT_GT(amp,0.002);
}

TEST(SignalProcessing, InitializeCWProcessing){
    float32_t * sinbuf = InitializeCWProcessing(15, &filters);
    EXPECT_FLOAT_EQ(sinbuf[50],sin(50*2*PI*750.0/24000.0));
}

void delay_us(int64_t delay_time_us){
    struct timeval tv;
    int64_t tnow_us;
    gettimeofday(&tv,NULL);
    int64_t tstart_us = (int64_t)(1000000 * tv.tv_sec) + (int64_t)(tv.tv_usec);
    while (true){
        gettimeofday(&tv,NULL);
        tnow_us = (int64_t)(1000000 * tv.tv_sec) + (int64_t)(tv.tv_usec);
        if ((tnow_us - tstart_us) > delay_time_us) break;
    }
}

TEST(SignalProcessing, MockMillis){
    StartMillis();
    delay_us(1000*100);
    int64_t duration_ms = millis();
    EXPECT_EQ(duration_ms,100);

    StartMillis();
    delay_us(1000*10);
    AddMillisTime(10);
    duration_ms = millis();
    EXPECT_EQ(duration_ms,20);

    StartMillis();
    delay_us(1000*10);
    SetMillisTime(500);
    duration_ms = millis();
    EXPECT_EQ(duration_ms,500);
}

TEST(SignalProcessing, CWProcessing){
    // I need to generate a stream of audio samples to represent characters
    int16_t wpm = 15;
    char msg[] = "ABCD ";
    char morseMsg[] = ".- -... -.-. -.. ";
    uint8_t ditdah[100] = {0};
    /*
     *     dit           = 1
     *     dah           = dit * 3
     *     inter-atom    = dit
     *     inter-letter  = dit * 3
     *     inter-word    = dit * 7
     */
    EEPROMData.decoderFlag = 1;
    InitializeCWProcessing(wpm, &filters);
    InitializeFilters(EEPROMData.spectrum_zoom,&filters);
    int16_t ddp = 0;
    for (size_t k = 0; k<sizeof(morseMsg); k++){
        switch (morseMsg[k]){
            case '.':
                ditdah[ddp++] = 1;
                ddp++;
                break;
            case '-':
                ditdah[ddp++] = 1;
                ditdah[ddp++] = 1;
                ditdah[ddp++] = 1;
                ddp++;
                break;
            case ' ':
                ddp += 2;
                break;
        }
    }
    // How long does each dit take?
    float32_t tatom_s = 60/(50 * (float32_t)wpm);
    // So the total time and number of frames needed is:
    float32_t total_time_s = sizeof(ditdah)*tatom_s; // 8
    uint32_t N_frames = (uint32_t)ceil(total_time_s / (256.0/24000.0)); // 750
    float32_t atoms_per_frame = (256.0/24000.0) / tatom_s; // 0.133333
    float32_t frames_per_atom = tatom_s / (256.0/24000.0); // 7.5
    float32_t samples_per_atom = tatom_s / (1.0/24000.0); // 1920

    // Generate a continuous tone for the duration of the message, and then
    // multiply by the ditdah atoms to change the amplitude.
    uint32_t phase = 0;
    float32_t I[256];
    float32_t Q[256];

    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = 256;
    data.sampleRate_Hz = SR[SampleRate].rate/filters.DF;

    uint32_t atomN;
    uint8_t mpoint = 0;
    FILE *file = fopen("CW_decoded_morse.txt", "w");    
    for (size_t k = 0; k<N_frames; k++){
        phase = CreateIQToneWithPhase(I, Q, 256, SR[SampleRate].rate/filters.DF, 
            CWToneOffsetsHz[EEPROMData.CWToneIndex], phase, 0.1);
        for (size_t j = 0; j<256; j++){
            atomN = (uint32_t)(((float32_t)k*256 + (float32_t)j)/samples_per_atom);
            I[j] = ditdah[atomN]*I[j];
        }
        // Set the time so that the millis() function is correct-ish
        SetMillisTime((uint64_t)(100.0+(float32_t)(k+1)*256.0/24000.0*1000.0));
        DoCWReceiveProcessing(&data, &filters);
        if (morseCharacterUpdated){
            fputc(morseCharacter, file);
            EXPECT_EQ(morseCharacter,msg[mpoint++]);
            morseCharacterUpdated = false;
        }
    }
    fclose(file);
}


void CW_filter_tone(float32_t toneFreq_Hz, DataBlock *dout, float32_t *gain){
    uint32_t Nsamples = 256;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;
   
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    DataBlock data;
    dout = &data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    uint32_t phase = 0;
    // Use four iterations for the filter to "warm up"
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    CWAudioFilter(&data, &filters);
    
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    CWAudioFilter(&data, &filters);
   
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    CWAudioFilter(&data, &filters);

    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, toneFreq_Hz, phase, 0.1);
    CWAudioFilter(&data, &filters);
    
    // Find the max value in data.I
    float32_t amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.I[i] > amp) amp = data.I[i];
    }
    *gain = amp/0.1;
}

TEST(SignalProcessing, CWAudioFilterBandpassPlot){
    // Generate a bunch of tones between fmin and fmax. Measure the output of
    // the filtered data for each tone. Measure the passband in this way.
    DataBlock *dout;
    float32_t fmin = 100.0;
    float32_t fmax = 6000.0;
    uint32_t Npoints = 201;
    float32_t fstep = (fmax - fmin)/(float32_t)Npoints;
    float32_t gain[Npoints];
    float32_t freq[Npoints];
    char strbuf[50];

    for (uint16_t bf = 0; bf < 5; bf++){
        EEPROMData.CWFilterIndex = bf;
        for (int i = 0; i<Npoints; i++){
            freq[i] = fmin + (float32_t)i*fstep;
            CW_filter_tone(freq[i], dout, &gain[i]);
        }
        sprintf(strbuf,"CWFilter_band_%d.txt",bf);
        WriteIQFile(freq,gain,strbuf,Npoints);
    }
}

TEST(SignalProcessing, CWAudioFilterBandpassTest){
    float32_t fc[] = {840,1080,1320,1800,2000};
    DataBlock *dout;
    float32_t gain;
    for (uint16_t bf = 0; bf < 5; bf++){
        EEPROMData.CWFilterIndex = bf;
        CW_filter_tone(fc[bf]*2, dout, &gain);
        EXPECT_LT(10*log10(gain),-35); // at least 35 dB of attnuation at 2x cutoff
        CW_filter_tone(fc[bf]*0.9, dout, &gain);
        EXPECT_NEAR(gain,1.0,0.005); // gain of 1 just below cutoff
    }
}

TEST(SignalProcessing, Interpolate){
    uint32_t Nsamples = READ_BUFFER_SIZE;
    float I[Nsamples];
    float Q[Nsamples];
    CLEAR_VAR(I);
    CLEAR_VAR(Q);
    float32_t sampleRate_Hz = SR[SampleRate].rate/filters.DF;
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = 256;
    data.sampleRate_Hz = sampleRate_Hz;

    // Do a sacrificial run to "warm up" the filters
    uint32_t phase = 0;
    phase = CreateIQToneWithPhase(I, Q, 256, sampleRate_Hz, 440.0, phase, 0.1);
    InterpolateReceiveData(&data, &filters);
    // Reset the data and run it for real
    CLEAR_VAR(I);
    CLEAR_VAR(Q);
    data.N = 256;
    data.sampleRate_Hz = sampleRate_Hz;
    phase = CreateIQToneWithPhase(I, Q, 256, sampleRate_Hz, 440.0, phase, 0.1);
    WriteIQFile(I,Q,"Interpolate_pre.txt",READ_BUFFER_SIZE);
    InterpolateReceiveData(&data, &filters);
    WriteIQFile(I,Q,"Interpolate_post.txt",READ_BUFFER_SIZE);
    EXPECT_EQ(data.N,READ_BUFFER_SIZE);
    EXPECT_EQ(data.sampleRate_Hz, SR[SampleRate].rate);    
}

TEST(SignalProcessing, VolumeToAmp){
    float32_t amp;
    amp = VolumeToAmplification(0);
    EXPECT_FLOAT_EQ(amp,0);
    amp = VolumeToAmplification(100);
    EXPECT_FLOAT_EQ(amp,5);
}

TEST(SignalProcessing, AdjustVolume){
    uint32_t Nsamples = 2048;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate;
   
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;

    uint32_t phase = 0;
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, 440.0, phase, 0.1);
    AdjustVolume(&data, &filters);
    float32_t amp = -100.0;
    for (size_t i=0; i<data.N; i++){
        if (data.I[i] > amp) amp = data.I[i];
    }
    EXPECT_FLOAT_EQ(amp, 0.1*filters.DF*VolumeToAmplification(EEPROMData.audioVolume));
}

TEST(SignalProcessing, PlayBuffer){
    Q_out_L.setName("PlayBuffer_L.txt");
    Q_out_R.setName("PlayBuffer_R.txt");
    InitializeFilters(SPECTRUM_ZOOM_1, &filters);
    uint32_t Nsamples = 2048;
    float I[Nsamples];
    float Q[Nsamples];
    float32_t sampleRate_Hz = SR[SampleRate].rate;
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = Nsamples;
    data.sampleRate_Hz = sampleRate_Hz;
    uint32_t phase = 0;
    phase = CreateIQToneWithPhase(I, Q, Nsamples, sampleRate_Hz, 440.0, phase, 0.1);
    PlayBuffer(&data);

    FILE* file = fopen("PlayBuffer_L.txt", "r");
    //FILE* file2 = fopen("PlayBuffer_R.txt", "r");
    int buffer[2048] = {0};
    int count = 0;
    //int b2;
    for (size_t k=0; k<2048; k++){
        fscanf(file, "%d", &buffer[k]);
        //fscanf(file2, "%d", &b2);
        //EXPECT_EQ(b2,buffer[k]);
    }
    int16_t amp = 0;
    for (size_t i=0; i<2048; i++){
        if (buffer[i] > amp) amp = buffer[i];
    }
    EXPECT_EQ(amp,(int16_t)(0.1*32768));
}

TEST(SignalProcessing, ReceiveProcessing){
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_out_L.setName("ReceiveOut_L.txt");
    Q_out_R.setName("ReceiveOut_R.txt");

    EEPROMData.agc = AGCOff;

    InitializeFilters(EEPROMData.spectrum_zoom,&filters);
    InitializeAGC(&agc,SR[SampleRate].rate/filters.DF);

    DataBlock *data;
    data = ReceiveProcessing("ReceiveProcessing_buffer1.txt");
    WriteIQFile(data->I,data->Q,"ReceiveProcessing_pass1_filtered_IQ.txt",2048);
    
    data = ReceiveProcessing("ReceiveProcessing_buffer2.txt");
    WriteIQFile(data->I,data->Q,"ReceiveProcessing_pass2_filtered_IQ.txt",2048);
   
    data = ReceiveProcessing("ReceiveProcessing_buffer3.txt");
    WriteIQFile(data->I,data->Q,"ReceiveProcessing_pass3_filtered_IQ.txt",2048);
    
    FILE* file = fopen("ReceiveOut_L.txt", "r");
    int buffer[2048] = {0};
    int count = 0;
    for (size_t k=0; k<2048; k++){
        fscanf(file, "%d", &buffer[k]);
    }
}

/*TEST(SignalProcessing, LongTerm){
    // Run a signal through the receive chain for a long period of time
    serialprintBegin("Terminal_LongTerm.txt");
    Q_in_L.setChannel(2);
    Q_in_R.setChannel(3);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_out_L.setName(nullptr);
    Q_out_R.setName(nullptr);

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    EEPROMData.agc = AGCOff;
    EEPROMData.nrOptionSelect = NROff;
    EEPROMData.fineTuneFreq_Hz = -48000.0;

    InitializeFilters(EEPROMData.spectrum_zoom,&filters);
    InitializeAGC(&agc,SR[SampleRate].rate/filters.DF);

    DataBlock *data;
    // 100 iterations is roughly 1 second. Run for 175 seconds.
    for (size_t k=0; k<175*100; k++){
        data = ReceiveProcessing(nullptr);
    }
    serialprintEnd();
    float32_t amp = -1e6;
    for (size_t i=0; i<data->N; i++){
        if (data->I[i] > amp) amp = data->I[i];
    }
}*/