#include "DSP_FFT.h"

/**
 * These functions that are placed in a stub to enable unit test substitution.
 * A different version of this file is compiled and linked when doing unit tests
 * rather than compiling for the Teensy. This is because the arm_cfft_f32 function,
 * which the Teensy uses, cannot run on a non-ARM processor because it contains ARM-
 * specific assembly code for speed optimizations. So we replace it with 
 * arm_cfft_radix2_f32 when doing unit tests, which produces the same output though
 * it is slightly slower.
 * 
    // We use a different FFT function during testing because arm_cfft_f32 calls a function 
    // for bit reversal that is written in assembly using the ARM ISA. This function won't 
    // compile on an x86 processor. Based on testing on the Teensy4.1, the arm_cfft_f32 
    // routine is about 38% faster than arm_cfft_radix2_f32.
    //
    // | Algorithm           | Time to complete one 512-point FFT |
    // |---------------------|------------------------------------|
    // | arm_cfft_f32        | 41 us                              |
    // | arm_cfft_radix2_f32 | 66 us                              |
    // Teensy 4.1 running at 600 MHz.
 */

// This is the version used on the Teensy

void FFT256Forward(float32_t *buffer){
    arm_cfft_f32(&arm_cfft_sR_f32_len256, buffer, 0, 1);
}

void FFT256Reverse(float32_t *buffer){
    arm_cfft_f32(&arm_cfft_sR_f32_len256, buffer, 1, 1);
}

void FFT512Forward(float32_t *buffer){
    arm_cfft_f32(&arm_cfft_sR_f32_len512, buffer, 0, 1);
}

void FFT512Reverse(float32_t *buffer){
    arm_cfft_f32(&arm_cfft_sR_f32_len512, buffer, 1, 1);
}

// This really doesn't belong here, but it saves the bother of creating another stub
void WriteIQFile(DataBlock *data, const char* fname){}
void WriteFloatFile(float32_t *data, size_t N, const char* fname){}