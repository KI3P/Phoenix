#include "SDT.h"

#ifdef FAST_TUNE
// The fast tune parameters
static uint64_t MS_temp, FT_delay, FT_last_time;
static bool FT_ON = false;
const uint64_t FT_cancel_ms = 500;  // time between steps above which FT is cancelled
const uint64_t FT_on_ms = 100;      // time between FTsteps below which increases the step size
const int32_t FT_trig = 4;          // number of short steps to trigger fast tune,
const int FT_step = 1000;           // Hz step in Fast Tune
static int64_t last_FT_step_size = 1;
static uint32_t FT_step_counter = 0;
#endif

/**
 * Adjust the fine tune frequency (2nd stage software mixer)
 * 
 * @param filter_change The positive or negative increment to the filter setting
 */
void AdjustFineTune(int32_t filter_change){
    #ifdef FAST_TUNE
    MS_temp = millis();
    FT_delay = MS_temp - FT_last_time;
    FT_last_time = MS_temp;
    if (FT_ON) {  // Check if FT should be cancelled (FT_delay>=FT_cancel_ms)
        if (FT_delay >= FT_cancel_ms) {
            FT_ON = false;
            ED.stepFineTune = last_FT_step_size;
        }
    } else {  //  FT is off so check for short delays
        if (FT_delay <= FT_on_ms) {
            FT_step_counter += 1;
        }
        if (FT_step_counter >= FT_trig) {
            last_FT_step_size = ED.stepFineTune;
            ED.stepFineTune = FT_step;
            FT_step_counter = 0;
            FT_ON = true;
        }
    }
    #endif  // FAST_TUNE

    ED.fineTuneFreq_Hz[ED.activeVFO] += ED.stepFineTune * filter_change;
    //Debug("Fine tune pre: " + String(ED.fineTuneFreq_Hz[ED.activeVFO]));
    // If the zoom level is 0, then the valid range of fine tune window is between
    // -samplerate/2 and +samplerate/2. 
    int32_t lower_limit = -(int32_t)(SR[SampleRate].rate)/2;
    int32_t upper_limit = (int32_t)(SR[SampleRate].rate)/2;
    if (ED.spectrum_zoom != 0) {
        // The fine tune frequency must stay within the visible tuning window, which
        // is determined by the zoom level. The visible window is determined by shifting
        // the spectrum by +48 kHz so the center is at EEPROMData.centerFreq_Hz - 48 kHz, 
        // then zooming in around the new center frequency.
        uint32_t visible_bandwidth = SR[SampleRate].rate / (1 << ED.spectrum_zoom);
        lower_limit = -(int32_t)visible_bandwidth/2;
        upper_limit = +(int32_t)visible_bandwidth/2;
        //Debug("Visible bandwidth: " + String(visible_bandwidth));
    }
    // Don't approach within the filter bandwidth of the band edge
    switch (ED.modulation[ED.activeVFO]){
        case LSB:
            lower_limit -= bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz; // FLoCut_Hz is negative
            break;
        case USB:
            upper_limit -= bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
            break;
        case AM:
        case SAM:
        case IQ:
        case DCF77:
            #define MAXABS(a, b) ((abs(a)) > (abs(b)) ? (abs(a)) : (abs(b)))
            int32_t edge_Hz = MAXABS(bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz,
                                    bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz); 
            lower_limit += edge_Hz;
            upper_limit -= edge_Hz;
            break;
    }
    //Debug("Upper limit: " + String(upper_limit));
    //Debug("Lower limit: " + String(lower_limit));
    if (-ED.fineTuneFreq_Hz[ED.activeVFO] > upper_limit) 
        ED.fineTuneFreq_Hz[ED.activeVFO] = -upper_limit;
    if (-ED.fineTuneFreq_Hz[ED.activeVFO] < lower_limit) 
        ED.fineTuneFreq_Hz[ED.activeVFO] = -lower_limit;
    //Debug("Fine tune post: " + String(ED.fineTuneFreq_Hz[ED.activeVFO]));

    // The fine tune is applied after the spectrum is shifted by samplerate/4. So the 
    // actual frequency in the RF domain is: TXRXFreq = centerFreq+fineTuneFreq-48kHz
    //ED.currentFreq[ED.activeVFO] = GetTXRXFreq_dHz()/100;
}

/**
 * Return the effective transmit/receive frequency, which is a combination of the center
 * frequency, the fine tune frequency, and the sample rate.
 * 
 * @return The effective transmit/receive frequency in units of Hz * 100
 */
int64_t GetTXRXFreq_dHz(void){
    int64_t val = 100*(ED.centerFreq_Hz[ED.activeVFO] - ED.fineTuneFreq_Hz[ED.activeVFO] - SR[SampleRate].rate/4);
    return val;
}

/**
 * Return the effective transmit/receive frequency, which is a combination of the center
 * frequency, the fine tune frequency, and the sample rate, for a particular VFO.
 * 
 * @return The effective transmit/receive frequency in units of Hz
 */
int64_t GetTXRXFreq(uint8_t vfo){
    int64_t val = (ED.centerFreq_Hz[vfo] - ED.fineTuneFreq_Hz[vfo] - SR[SampleRate].rate/4);
    return val;
}

/**
 * Return the CW transmit frequency, which is a combination of the RX/TX frequency and the
 * CW tone offset.
 * 
 * @return The CW tone transmit frequency in units of Hz * 100
 */
int64_t GetCWTXFreq_dHz(void){
    int64_t txrx = GetTXRXFreq_dHz();
    int64_t offset = (int64_t)(100*CWToneOffsetsHz[ED.CWToneIndex]);
    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB) {
        return txrx - offset;
    } else {
        return txrx + offset;
    }
}

/**
 * Set the fine tune frequency to 0 Hz and change the center frequency such that 
 * the RXTX frequency remains the same.
 */
void ResetTuning(void){
    ED.centerFreq_Hz[ED.activeVFO] = ED.centerFreq_Hz[ED.activeVFO] - ED.fineTuneFreq_Hz[ED.activeVFO];
    ED.fineTuneFreq_Hz[ED.activeVFO] = 0.0;
    return;
}

int8_t GetBand(int64_t freq){
    for(uint8_t i = FIRST_BAND; i <= LAST_BAND; i++){
        if(freq >= bands[i].fBandLow_Hz && freq <= bands[i].fBandHigh_Hz){
            return i;
        }
    }
    return -1; // Frequency not within one of the defined ham bands
}
