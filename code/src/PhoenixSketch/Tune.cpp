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
    //Debug("Fine tune pre: " + String(EEPROMData.fineTuneFreq_Hz));
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
    //Debug("Upper limit: " + String(upper_limit));
    //Debug("Lower limit: " + String(lower_limit));
    if (ED.fineTuneFreq_Hz[ED.activeVFO] > upper_limit) 
        ED.fineTuneFreq_Hz[ED.activeVFO] = upper_limit;
    if (ED.fineTuneFreq_Hz[ED.activeVFO] < lower_limit) 
        ED.fineTuneFreq_Hz[ED.activeVFO] = lower_limit;
    //Debug("Fine tune post: " + String(EEPROMData.fineTuneFreq_Hz));

    // The fine tune is applied after the spectrum is shifted by samplerate/4. So the 
    // actual frequency in the RF domain is: TXRXFreq = centerFreq+fineTuneFreq-48kHz
    //ED.currentFreq[ED.activeVFO] = GetTXRXFreq_dHz()/100;
}

/**
 * Switch the active VFO. This involves retuning the radio.
 */
void ChangeVFO(void){
    if (ED.activeVFO == 0){
        ED.activeVFO = 1;
    }else{
        ED.activeVFO = 0;
    }
    ChangeTune();
}

/**
 * Return the effective transmit/receive frequency, which is a combination of the center
 * frequency, the fine tune frequency, and the sample rate.
 * 
 * @return The effective transmit/receive frequency in units of Hz * 100
 */
int64_t GetTXRXFreq_dHz(void){
    int64_t val = 100*(ED.centerFreq_Hz[ED.activeVFO] + ED.fineTuneFreq_Hz[ED.activeVFO] - SR[SampleRate].rate/4);
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
 * Tune the receiver to the active VFO selection.
 */
void ChangeTune(void){
    SelectLPFBand(ED.currentBand[ED.activeVFO]);
    SetBPFBand(ED.currentBand[ED.activeVFO]);
    UpdateTuneState();
    SetAntenna(ED.currentBand[ED.activeVFO]);
    UpdateFIRFilterMask(&filters);
}

int8_t GetBand(int64_t freq){
    for(uint8_t i = 0; i < NUMBER_OF_BANDS; i++){
        if(freq >= bands[i].fBandLow_Hz && freq <= bands[i].fBandHigh_Hz){
            return i;
        }
    }
    return -1; // Frequency not within one of the defined ham bands
}

void ChangeBand(void){
    ED.centerFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][0];
    ED.fineTuneFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][1];
    ChangeTune();
}

static TuneState tuneState = TuneReceive;
//static TuneState oldTuneState = TuneReceive;

/*think about frequency control. How will this change as I switch between SSB mode and CW mode?
and between SSB receive and SSB transmit mode? it changes from state to state.
* CW/SSB Receive:  RXfreq = centerFreq_Hz + fineTuneFreq_Hz - SampleRate/4
* SSB Transmit:    TXfreq = centerFreq_Hz
* CW Transmit:     TXfreq = centerFreq_Hz + fineTuneFreq_Hz - SampleRate/4 -/+ CWToneOffset
*/

void HandleTuneState(TuneState tuneState){
    switch (tuneState){
        case TuneReceive:{
            // CW/SSB Receive:  RXfreq = centerFreq_Hz + fineTuneFreq_Hz - SampleRate/4
            int64_t newFreq = ED.centerFreq_Hz[ED.activeVFO]*100;
            SetSSBVFOFrequency( newFreq );
            break;
        }
        case TuneSSBTX:{
            // SSB Transmit:    TXfreq = centerFreq_Hz
            int64_t newFreq = GetTXRXFreq_dHz();
            SetSSBVFOFrequency( newFreq );
            break;
        }
        case TuneCWTX:{
            // CW Transmit:     TXfreq = centerFreq_Hz + fineTuneFreq_Hz - SampleRate/4 -/+ CWToneOffset
            int64_t newFreq = GetCWTXFreq_dHz();
            SetCWVFOFrequency( newFreq );

            break;
        }
    }
}

void UpdateTuneState(void){
    switch (modeSM.state_id){
        case (ModeSm_StateId_CW_RECEIVE):
        case (ModeSm_StateId_SSB_RECEIVE):{
            tuneState = TuneReceive;
            break;
        }
        case (ModeSm_StateId_SSB_TRANSMIT):{
            tuneState = TuneSSBTX;
            break;
        }
        case (ModeSm_StateId_CW_TRANSMIT_DIT_MARK):
        case (ModeSm_StateId_CW_TRANSMIT_DAH_MARK):
        case (ModeSm_StateId_CW_TRANSMIT_MARK):
        case (ModeSm_StateId_CW_TRANSMIT_SPACE):
        case (ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE):
        case (ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT):{
            tuneState = TuneCWTX;
            break;
        }

        //case (ModeSm_StateId_CALIBRATE_FREQUENCY):
        //case (ModeSm_StateId_CALIBRATE_RX_IQ):
        //case (ModeSm_StateId_CALIBRATE_TX_IQ):
        //case (ModeSm_StateId_CALIBRATE_CW_PA):
        //case (ModeSm_StateId_CALIBRATE_SSB_PA):{
        //    break;
        //}

        default:{
            Debug("Unhandled modeSM.state_id state in UpdateTuneState!");
            char strbuf[10];
            sprintf(strbuf, "> %lu",(uint32_t)modeSM.state_id);
            Debug(strbuf);
            break;
        }
    }
    HandleTuneState(tuneState);
}