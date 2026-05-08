/* 
Copyright (C) 2026 T41 EP Software Contributors
See Contributors.txt for list of known authors.

This file is part of Phoenix.

Phoenix is free software: you can redistribute it and/or modify it under the 
terms of the GNU General Public License as published by the Free Software 
Foundation, either version 3 of the License, or (at your option) any later version.

Phoenix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Phoenix. 
If not, see <https://www.gnu.org/licenses/>.
*/

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

// These really don't belong here, but it saves the bother of creating another stub.
// These functions are used by the unit tests to examine data as it flows through the
// DSP chains.
void WriteIQFile(DataBlock *data, const char* fname){} // do nothing
void WriteFloatFile(float32_t *data, size_t N, const char* fname){} // do nothing