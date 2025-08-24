#include "../src/PhoenixSketch/DSP_FFT.h"

/**
 * These functions that are placed in a stub to enable unit test substitution.
 * A different version of this file is compiled and linked when doing unit tests
 * rather than compiling for the Teensy. This is because the arm_cfft_f32 function,
 * which the Teensy uses, cannot run on a non-ARM processor because it contains ARM-
 * specific assembly code for speed optimizations. So we replace it with 
 * arm_cfft_radix2_f32 when doing unit tests, which produces the same output though
 * it is slightly slower.
 */

// This is the version used in test mode

arm_cfft_radix2_instance_f32 Sfor;
arm_cfft_radix2_instance_f32 Srev;

void InitFFT256(void){
    arm_cfft_radix2_init_f32(&Sfor, 256, 0, 1);
    arm_cfft_radix2_init_f32(&Srev, 256, 1, 1);
}

void FFT256Forward(float32_t *buffer){
    arm_cfft_radix2_f32(&Sfor,buffer);
}

void FFT256Reverse(float32_t *buffer){
    arm_cfft_radix2_f32(&Srev,buffer);
}
