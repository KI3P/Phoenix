#include "DSP_FFT.h"

/**
 * These functions that are placed in a stub to enable unit test substitution.
 * A different version of this file is compiled and linked when doing unit tests
 * rather than compiling for the Teensy. This is because the arm_cfft_f32 function,
 * which we use when computing FFTs on the Teensy, cannot run on an x86 processor 
 * because it contains ARM ISA-specific assembly code in the bit reversal function
 * for speed optimizations. So we replace it with arm_cfft_radix2_f32 when doing unit
 * tests, which produces the same output though it is slightly slower.
 * 
 * Based on testing on the Teensy4.1, the arm_cfft_f32 routine is about 38% faster 
 * than arm_cfft_radix2_f32.
 * 
 * | Algorithm           | Time to complete one 512-point FFT |
 * |---------------------|------------------------------------|
 * | arm_cfft_f32        | 41 us                              |
 * | arm_cfft_radix2_f32 | 66 us                              |
 * 
 * Teensy 4.1 running at 600 MHz.
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

// These really don't belong here, but it saves the bother of creating another stub
void WriteIQFile(DataBlock *data, const char* fname){} // do nothing
void WriteFloatFile(float32_t *data, size_t N, const char* fname){} // do nothing