#include "CWProcessing.h"

float32_t float_buffer_CW[256];
float32_t sinBuffer[256];
float32_t float_Corr_Buffer[511];
static float32_t aveCorrResult;
static uint32_t CWLevelTimer, CWLevelTimerOld;

// charProcessFlag means a character is being decoded.  
// blankFlag indicates a blank has already been printed.
bool charProcessFlag, blankFlag;
static enum MorseStates decodeStates = state0;
static int currentTime, interElementGap, noSignalTimeStamp;
static int64_t signalStart, signalEnd;
static int64_t gapLength;//, gapEnd, gapStart; // Time for noise measures
static float32_t thresholdGeometricMean = 140.0;  // This changes as decoder runs
static uint64_t ditLength, dahLength;
static int64_t signalElapsedTime;
static uint8_t currentDashJump = DECODER_BUFFER_SIZE;
static uint8_t currentDecoderIndex = 0;
static int32_t gapHistogram[HISTOGRAM_ELEMENTS];
static int32_t gapAtom, topGapIndex, topGapIndexOld, gapChar;
static uint8_t endGapFlag;
uint8_t valFlag = 0;
int64_t valRef1, gapRef1, valRef2;
int64_t aveDitLength = 80;
int64_t aveDahLength = 200;
int64_t signalStartOld = 0;
int32_t signalHistogram[HISTOGRAM_ELEMENTS];
char *bigMorseCodeTree = (char *)"-EISH5--4--V---3--UF--------?-2--ARL---------.--.WP------J---1--TNDB6--.--X/-----KC------Y------MGZ7----,Q------O-8------9--0----";
//                                012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678
//                                         10        20        30        40        50        60        70        80        90       100       110       120


float32_t * InitializeCWProcessing(uint32_t wpm, FilterConfig *filters){
    float32_t theta = 0.0;
    // phs = 2 * PI * freqSideTone / 24000
    float32_t phs = 2.0 * PI * CWToneOffsetsHz[EEPROMData.CWToneIndex] 
            / ((float32_t)SR[SampleRate].rate/(float32_t)filters->DF);
    for (uint32_t kf = 0; kf < READ_BUFFER_SIZE/filters->DF; kf++) {
        theta = kf * phs;  
        sinBuffer[kf] = sin(theta);
    }
    ResetHistograms();
    SetDitLength(wpm);
    return sinBuffer;
}

/**
 * Process CW specific signals
 */
void DoCWReceiveProcessing(DataBlock *data, FilterConfig *filters) {
    float32_t goertzelMagnitude;
    uint8_t audioTemp;
    float32_t corrResult;
    uint32_t corrResultIndex;
    float32_t combinedCoeff;

    // Park McClellan FIR filter const Group delay
    // Note that data-Q contains duplicate data as this is after demod
    arm_fir_f32(&filters->FIR_CW_Decode, data->I, float_buffer_CW, 256);

    if (EEPROMData.decoderFlag == 1) {
        // Calculate correlation between calc sine and incoming signal
        arm_correlate_f32(float_buffer_CW, 256, sinBuffer, 256, float_Corr_Buffer);
        arm_max_f32(float_Corr_Buffer, 511, &corrResult, &corrResultIndex);
        //running average of corr coeff.
        aveCorrResult = .7 * corrResult + .3 * aveCorrResult;
        
        // Calculate Goertzel Magnitude of incomming signal
        goertzelMagnitude = goertzel_mag(256, CWToneOffsetsHz[EEPROMData.CWToneIndex], 
                                    data->sampleRate_Hz, float_buffer_CW);

        // Combine Correlation and Gowetzel Coefficients
        combinedCoeff = 10 * aveCorrResult * 100 * goertzelMagnitude;

        // If have a reasonable corr coeff, >50, then we have a keeper, changed CW decode "lock" indicator
        if (combinedCoeff > 50) {
            audioTemp = 1;
            CWLocked = true;
        } else {
            audioTemp = 0;
            CWLevelTimer = millis();
            if (CWLevelTimer - CWLevelTimerOld > 2000) {
                CWLevelTimerOld = millis();
                CWLocked = false;
            }
        }
        DoCWDecoding(audioTemp);
    }
}


/**
 * Calculate Goertzal Algorithn to enable decoding CW
 * 
 * @param numSamples Number of sample in data array
 * @param TARGET_FREQUENCY Frequency for which the magnitude of the transform is to be found
 * @param SAMPLING_RATE Sampling rate in our case 24ksps
 * @param *data Pointer to input data array
 * @return magnitude Magnitude of the transform at the target frequency
 */
float32_t goertzel_mag(uint32_t numSamples, int32_t TARGET_FREQUENCY, uint32_t SAMPLING_RATE, float32_t *data) {
    int k;
    float floatnumSamples;
    float omega, sine, cosine, coeff, q0, q1, q2, magnitude, real, imag;

    float scalingFactor = numSamples / 2.0;

    floatnumSamples = (float)numSamples;
    k = (int)(0.5 + ((floatnumSamples * TARGET_FREQUENCY) / SAMPLING_RATE));
    omega = (2.0 * M_PI * k) / floatnumSamples;
    sine = sin(omega);
    cosine = cos(omega);
    coeff = 2.0 * cosine;
    q0 = 0;
    q1 = 0;
    q2 = 0;

    for (uint32_t i = 0; i < numSamples; i++) {
        q0 = coeff * q1 - q2 + data[i];
        q2 = q1;
        q1 = q0;
    }
    real = (q1 - q2 * cosine) / scalingFactor;  // calculate the real and imaginary results scaling appropriately
    imag = (q2 * sine) / scalingFactor;

    magnitude = sqrtf(real * real + imag * imag);
    return magnitude;
}

/**
 * Called when in CW mode to decode morse. Function assumes:
 * 
 *     dit           = 1
 *     dah           = dit * 3
 *     inter-atom    = dit
 *     inter-letter  = dit * 3
 *     inter-word    = dit * 7
 *
 * You can distinguish between dah and inter-letter by presence/absence of signal. Same for inter-atom.
 * 
 * @param audioValue        the strength of audio signal
 */
void DoCWDecoding(uint8_t audioValue) {
    static int64_t oldTime = millis();
    switch (decodeStates) {
        case state0:{
            // State 0.  Detects start of signal and starts timer.
            // Detect signal and redirect to appropriate state.
            if (audioValue == 1) {
                signalStart = millis(); // Time stamp beginning of signal.
                decodeStates = state1;  // Go to "signalStart" state.
                // Calculate the time gap between the start of this new signal and the end of the last one.
                gapLength = signalStart - signalEnd;  
                if (gapLength > LOWEST_ATOM_TIME // range
                    && (uint32_t)gapLength < (uint32_t)(thresholdGeometricMean * 3)
                    && signalStart - oldTime > 5000L) {    // Only call histogram every 5 seconds
                    DoGapHistogram(gapLength);             // Map the gap in the signal
                    oldTime = signalStart;                 // Reset old time
                }
                break;
            }
            noSignalTimeStamp = millis();
            interElementGap = noSignalTimeStamp - signalEnd;
            // use thresholdGeometricMean??? was ditLength. End of character!  65 * 2
            if ((interElementGap > ditLength * 1.95) && charProcessFlag) {  
                decodeStates = state5;   // Character ended, print it!
                break;
            }
            // A big gap, print a blank, but don't repeat a blank.  85 * 3.5
            if (interElementGap > ditLength * 4.5 && not blankFlag && not charProcessFlag) {  
                decodeStates = state6;
                break;
            }
            decodeStates = state0;  // Stay in state0; no signal.
            break;                                                                                                 // End state0
        }
        case state1:{
            // Times a signal and measures its duration.  The next state determines if the signal is a dit or a dah.
            if (audioValue == 0) {
                currentTime       = millis();
                signalElapsedTime = currentTime - signalStart;     // Calculate the duration of the signal.
                // Ignore short noisy signal bursts:
                if (signalElapsedTime < LOWEST_ATOM_TIME) {        // A hiccup or a real signal?  Make this a fraction of ditLength instead???
                    decodeStates    = state0;                      // False signal, start over.
                    break;
                }
                if (signalElapsedTime > LOWEST_ATOM_TIME           // Valid elapsed time?
                    && signalElapsedTime < HISTOGRAM_ELEMENTS
                    && currentTime - oldTime > 5000L) {            // Only call histogram every 5 seconds
                    DoSignalHistogram(signalElapsedTime);          // Yep
                    oldTime = currentTime;                         // Reset old time
                }
                signalEnd         = currentTime;                   // Time gap to next signal.
                decodeStates      = state2;                        // Proceed to state2.  A timed signal is available and must be processed.
                break;
            }
            decodeStates = state1;  // Signal still present, stay in state1.
            break;                  // End state1
        }
        case state2:{
            // Determine if a timed signal was a dit or a dah and increment the decode tree.
            if (signalElapsedTime > (0.5 * ditLength)) {              // Use the geometric mean instead of ditLength???
                currentDashJump = currentDashJump >> 1;                 // Fast divide by 2
                if (signalElapsedTime < (int)thresholdGeometricMean) {  // It was a dit
                    charProcessFlag = true;                               
                    currentDecoderIndex++;
                } else {  // It's a dah!
                    charProcessFlag = true;
                    currentDecoderIndex += currentDashJump;
                }
            }
            decodeStates = state0;  // Begin process again.
            break;                  // End state2
        }
        case state5:{
            // Display the character
            morseCharacter = bigMorseCodeTree[currentDecoderIndex];  // This always prints.  How do blanks get printed.
            morseCharacterUpdated = true;
            currentDecoderIndex = 0;                                       //Reset everything if char or word
            currentDashJump     = DECODER_BUFFER_SIZE;
            charProcessFlag     = false;  // Char printed and no longer in progress.
            decodeStates        = state0;    // Start process for next incoming character.
            blankFlag           = false;
            break;                                                    // End state5
        }
        case state6:{
            //  Blank printing state.
            morseCharacter = ' '; 
            morseCharacterUpdated = true;
            blankFlag = true;
            decodeStates = state0;  // Start process for next incoming character.
            break;
        }
        default:
        break;
    }
}

/**
 * This function creates a distribution of the gaps between signals, expressed
 * in milliseconds. The result is a tri-modal distribution around three timings:
 *    1. inter-atom time (one dit length)
 *    2. inter-character (three dit lengths)
 *    3. word end (seven dit lengths)
 * 
 * @param gapLen the duration of the signal gap (ms)
 */
void DoGapHistogram(int64_t gapLen) {
    int32_t tempAtom, tempChar;
    int32_t atomIndex, charIndex, firstDit, temp;
    uint32_t offset;

    if (gapHistogram[gapLen] > 10) {  // Need over 1 so we don't have fractional value
        for (int k = 0; k < HISTOGRAM_ELEMENTS; k++) {
        gapHistogram[k] = (uint32_t)(.8 * (float)gapHistogram[k]);
        }
    }
    gapHistogram[gapLen]++;  // Add new signal to distribution
    atomIndex = charIndex = 0;
    if (gapLen <= thresholdGeometricMean) {                                                                                 // Find new dit length
        JackClusteredArrayMax(gapHistogram, (uint32_t)thresholdGeometricMean, &tempAtom, &atomIndex, &firstDit, (int32_t)1);  // Find max dit gap
        if (atomIndex) {                                                                                                    // if something found
            gapAtom = atomIndex;
        }
        for (int j = 0; j < HISTOGRAM_ELEMENTS; j++) {                        // count down
            if (gapHistogram[HISTOGRAM_ELEMENTS - j] > 0 && endGapFlag == 0) {  //Look for non-zero entries in the histogram
                if (HISTOGRAM_ELEMENTS - j < gapAtom * 2) {                       // limit search to probable gapAtom entries
                topGapIndex = HISTOGRAM_ELEMENTS - j;                           //Upper end of gapAtom range
                endGapFlag = 1;                                                 // set flag so we know tha this is the top of the gapAtom range
                }
            }
            if (topGapIndex > 2 * gapAtom) topGapIndex = topGapIndexOld;  // discard outliers
        }
        endGapFlag = 0;                //reset flag
        topGapIndexOld = topGapIndex;  //Keep good value for reference
    } else {                         // dah calculation
        if (gapLen <= thresholdGeometricMean * 2) {
        offset = (uint32_t)(thresholdGeometricMean * 2);  // Find number of elements to check
        JackClusteredArrayMax(&gapHistogram[(int32_t)thresholdGeometricMean + 1], offset, &tempChar, &charIndex, &temp, (int32_t)3);
        if (charIndex)  // if something found
            gapChar = charIndex;
        }
    }
    if (atomIndex) {
        gapAtom = atomIndex;
    }
    if (charIndex) {
        gapChar = charIndex;
    }
}


/**
 * Establish the dit length for code transmission. Crucial since all spacing is 
 * done using dit length. Sets the value of the shared variable ditLength
 * 
 * @param wpm Words per minute
 */
void SetDitLength(uint32_t wpm) {
    ditLength = 1200 / wpm;
    EEPROMData.currentWPM = 1200 / ditLength;
}

/**
 * This function replaces the arm_max_float32() function that finds the maximum element in an array.
 * The histograms are "fuzzy" in the sense that dits and dahs "cluster" around a maximum value rather
 * than having a single max value. This algorithm looks at a given cell and the adds in the previous
 * (index - 1) and next (index + 1) cells to get the total for that index.
 * 
 * @param *array         the base address of the array to search
 * @param elements       the number of elements of the array to examine
 * @param *maxCount      the largest clustered value found
 * @param *maxIndex      the index of the center of the cluster
 * @param *firstNonZero  the first cell that has a non-zero value
 * @param clusterSpread  tells how far previous and ahead elements are to be included in the measure.
 *                           Must be an odd integer > 1.
 */
void JackClusteredArrayMax(int32_t *array, int32_t elements, int32_t *maxCount, int32_t *maxIndex, int32_t *firstNonZero, int32_t spread) {
    int32_t i, j, clusteredIndex;
    int32_t clusteredMax, temp;

    *maxCount = '\0';  // Reset to empty
    *maxIndex = '\0';

    clusteredMax = 0;
    clusteredIndex = -1;  // Now we can check for an error

    for (i = spread; i < elements - spread; i++) {  // Start with 1 so we can look at the previous element's value
        temp = 0;
        for (j = i - spread; j <= i + spread; j++) {
            temp += array[j];
            ;  // Include adjacent elements
        }

        if (temp >= clusteredMax) {
            clusteredMax = temp;
            clusteredIndex = i;
        }
    }
    if (clusteredIndex > 0) {
        *maxCount = array[clusteredIndex];
        *maxIndex = clusteredIndex;
    }
}

/**
 * This function creates a distribution of the dit and dahs lengths, expressed in
 * milliseconds. The result is a bi-modal distribution around those two timings. The
 * modal value is then used for the timing of the decoder. The range should be between 20
 * (60wpm) and 240 (5wpm)
 * 
 * @param val        the strength of audio signal
 * 
 */
void DoSignalHistogram(int64_t val) {
    float compareFactor = 2.0;
    int32_t firstNonEmpty;
    int32_t tempDit, tempDah;
    int32_t offset;
    //float32_t thresholdArithmeticMean;

    if (valFlag == 0) {
        valRef1 = signalElapsedTime;
        signalStartOld = millis();
        valFlag = 1;
    }

    if (millis() - signalStartOld > LOWEST_ATOM_TIME && valFlag == 1) {
        gapRef1 = gapLength;
        valRef2 = signalElapsedTime;
        valFlag = 0;
    }

    if ((valRef2 >= valRef1 * compareFactor && gapRef1 <= valRef1 * compareFactor)
        || (valRef1 >= valRef2 * compareFactor && gapRef1 <= valRef2 * compareFactor)) {
        // See if consecutive signal lengths in approximate dit to dah ratio and which one is larger
        if (valRef2 >= valRef1) {
            aveDitLength = (long)(0.9 * aveDitLength + 0.1 * valRef1);  //Do some dit length averaging
            aveDahLength = (long)(0.9 * aveDahLength + 0.1 * valRef2);
        } else {
            aveDitLength = (long)(0.9 * aveDitLength + 0.1 * valRef2);  // Use larger one. Note reversal of calc order
            aveDahLength = (long)(0.9 * aveDahLength + 0.1 * valRef1);  // Do some dah length averaging
        }
    }
    thresholdGeometricMean = sqrt(aveDitLength * aveDahLength);    //calculate geometric mean
    //thresholdArithmeticMean = (aveDitLength + aveDahLength) >> 1;  // Fast divide by 2 on integer data

    signalHistogram[val]++;  // Don't care which half it's in, just put it in

    offset = (uint32_t)thresholdGeometricMean - 1;  // Only do cast once
    // Dit calculation
    // 2nd parameter means we only look for dits below the geomean.

    for (int32_t j = (int32_t)thresholdGeometricMean; j; j--) {
        if (signalHistogram[j] != 0) {
            firstNonEmpty = j;
            break;
        }
    }

    JackClusteredArrayMax(signalHistogram, offset, &tempDit, (int32_t *)&ditLength, &firstNonEmpty, (int32_t)1);
    // dah calculation
    // Elements above the geomean. Note larger spread: higher variance
    JackClusteredArrayMax(&signalHistogram[offset], HISTOGRAM_ELEMENTS - offset, &tempDah, (int32_t *)&dahLength, &firstNonEmpty, (uint32_t)3);
    dahLength += (uint32_t)offset;

    if (tempDit > SCALE_CONSTANT && tempDah > SCALE_CONSTANT) {  //Adaptive dit signalHistogram[]
        for (int k = 0; k < HISTOGRAM_ELEMENTS; k++) {
            signalHistogram[k] = ADAPTIVE_SCALE_FACTOR * signalHistogram[k];
        }
    }
}

/**
 * This function uses the current WPM to set an estimate ditLength any time the tune
 * endcoder is changed
 */
void ResetHistograms() {
    gapAtom = 80;
    ditLength = 80;  // Start with 15wpm ditLength
    gapChar = 240;
    dahLength = 240;
    thresholdGeometricMean = 160;  // Use simple mean for starters so we don't have 0
    aveDitLength = ditLength;
    aveDahLength = dahLength;
    valRef1 = 0;
    valRef2 = 0;
    // Clear graph arrays
    memset(signalHistogram, 0, HISTOGRAM_ELEMENTS * sizeof(uint32_t));
    memset(gapHistogram, 0, HISTOGRAM_ELEMENTS * sizeof(uint32_t));
    EEPROMData.currentWPM = 1200 / ditLength;
    //UpdateWPMField();
}

void CWAudioFilter(DataBlock *data, FilterConfig *filters){
    switch (EEPROMData.CWFilterIndex) {
        case 0: // 0.8 KHz
            arm_biquad_cascade_df2T_f32(&filters->S1_CW_AudioFilter1, data->I, data->Q, data->N);
            arm_copy_f32(data->Q, data->I, data->N);
            break;
        case 1: // 1.0 KHz
            arm_biquad_cascade_df2T_f32(&filters->S1_CW_AudioFilter2, data->I, data->Q, data->N);
            arm_copy_f32(data->Q, data->I, data->N);
            break;
        case 2: // 1.3 KHz
            arm_biquad_cascade_df2T_f32(&filters->S1_CW_AudioFilter3, data->I, data->Q, data->N);
            arm_copy_f32(data->Q, data->I, data->N);
            break;
        case 3: // 1.8 KHz
            arm_biquad_cascade_df2T_f32(&filters->S1_CW_AudioFilter4, data->I, data->Q, data->N);
            arm_copy_f32(data->Q, data->I, data->N);
            break;
        case 4: // 2.0 KHz
            arm_biquad_cascade_df2T_f32(&filters->S1_CW_AudioFilter5, data->I, data->Q, data->N);
            arm_copy_f32(data->Q, data->I, data->N);
            break;
        case 5:  //Off
            break;
    }
}