#include "SDT.h"

static ModeSm_StateId previousRadioState = ModeSm_StateId_ROOT;
static RFBoardState rfBoardState = RFBoardReceive;
static RFBoardState oldrfBoardState = RFBoardReceive;

/**
 * Initialize the RF board by calling the initialization functions for each 
 * of the modules hosted on the RF board:
 *   > Attenuators
 *   > SSB VFO
 *   > CW VFO
 *   > Transmit modulation control
 *   > Calibration control
 *   > RXTX control
 */
errno_t InitializeRFBoard(void){
    errno_t err = InitAttenuation();
    err += InitCalFeedbackControl();
    err += InitTXModulation();
    err += InitVFOs();
    err += InitRXTX();

    // force the initialization to RF receive
    oldrfBoardState = RFBoardSSBTransmit;
    HandleRFBoardStateChange(RFBoardReceive); // updates oldrfBoardState
    previousRadioState = ModeSm_StateId_SSB_RECEIVE;
    return err;
}

////////////////////////////////////////////////////////////////////////////////////
// State machine code
////////////////////////////////////////////////////////////////////////////////////

ModeSm_StateId GetRFBoardPreviousState(void){
    return previousRadioState;
}

void HandleRFBoardStateChange(RFBoardState newState){
    if (newState == oldrfBoardState){
        return;
    }
    // Following the state diagram in RF_board_api.drawio, implement the actions
    // required to enter the new state.
    switch (newState){
        case RFBoardReceive:{
            // Set GPA state to appropriate value
            SetRXAttenuation( ED.RAtten[ED.currentBand[ED.activeVFO]] );
            // Set clockEnableCW to LO
            DisableCWVFOOutput();
            // Set cwState to LO
            CWoff();
            // Set frequencySSB_Hz to appropriate value
            //SetSSBVFOFrequency( ED.centerFreq_Hz[ED.activeVFO]*100 );
            UpdateTuneState();
            // Set driveCurrentSSB_mA to appropriate value
            // > This does not change after initialization, so do nothing
            // Set clockEnableSSB to HI (SSB)
            EnableSSBVFOOutput();
            // Set calFeedbackState to LO
            DisableCalFeedback();
            // Set rxtxState to LO (RX)
            SelectRXMode();
            break;
        }
        case RFBoardSSBTransmit:{
            // Set GPB state to appropriate value
            SetTXAttenuation( ED.XAttenSSB[ED.currentBand[ED.activeVFO]] );
            // Set clockEnableCW to LO
            DisableCWVFOOutput();
            // Set cwState to LO
            CWoff();
            // Set frequencySSB_Hz to the TXRX frequency
            //SetSSBVFOFrequency( GetTXRXFreq_dHz() );
            UpdateTuneState();
            // Set driveCurrentSSB_mA to appropriate value
            // > This does not change after initialization, so do nothing
            // Set clockEnableSSB to HI
            EnableSSBVFOOutput();
            // Set modulationState to HI (SSB)
            SelectTXSSBModulation();
            // Set calFeedbackState to LO
            DisableCalFeedback();
            // Set rxtxState to HI (TX)
            SelectTXMode();
            break;
        }
        case RFBoardCWMark:{
            // Set GPB state to appropriate value
            SetTXAttenuation( ED.XAttenCW[ED.currentBand[ED.activeVFO]] );
            // Set clockEnableSSB to LO
            DisableSSBVFOOutput();
            // Set frequencyCW_Hz to appropriate value
            //SetCWVFOFrequency( GetCWTXFreq_dHz() );
            UpdateTuneState();
            // Set driveCurrentCW_mA to appropriate value
            // > This does not change after initialization, so do nothing
            // Set clockEnableCW to HI
            EnableCWVFOOutput();
            // Set cwState to HI
            CWon();
            // Set rxtxState to HI (TX)
            SelectTXMode();
            // Set modulationState to LO (CW)
            SelectTXCWModulation();
            // Set calFeedbackState to LO
            DisableCalFeedback();
            break;
        }
        case RFBoardCWSpace:{
            // Set GPB state to appropriate value
            SetTXAttenuation( ED.XAttenCW[ED.currentBand[ED.activeVFO]] );
            // Set clockEnableSSB to LO
            DisableSSBVFOOutput();
            // Set frequencyCW_Hz to appropriate value
            //SetCWVFOFrequency( GetCWTXFreq_dHz() );
            UpdateTuneState();
            // Set driveCurrentCW_mA to appropriate value
            // > This does not change after initialization, so do nothing
            // Set clockEnableCW to HI
            EnableCWVFOOutput();
            // Set cwState to LO
            CWoff();
            // Set rxtxState to HI (TX)
            SelectTXMode();
            // Set modulationState to LO (CW)
            SelectTXCWModulation();
            // Set calFeedbackState to LO
            DisableCalFeedback();
            break;
        }
        case RFBoardCalIQ:{
            // Set GPA state to appropriate value
            // Set GPB state to appropriate value
            // Set frequencySSB_Hz to appropriate value
            // Set driveCurrentSSB_mA to appropriate value
            // Set clockEnableSSB to HI
            // Set frequencyCW_Hz to appropriate value
            // Set driveCurrentCW_mA to appropriate value
            // Set clockEnableCW to HI
            // Set cwState to LO
            // Set rxtxState to LO
            // Set modulationState to LO
            // Set calFeedbackState to LO
            break;
        }
    }
    oldrfBoardState = newState;
}

void UpdateRFBoardState(void){
    if (modeSM.state_id == previousRadioState){
        // Already in this state, no need to change
        return;
    }
    // Several transceiver states map to the same RF board state. Handle this mapping
    // and call the function to handle the new RF board state.
    switch (modeSM.state_id){
        case (ModeSm_StateId_CW_RECEIVE):
        case (ModeSm_StateId_SSB_RECEIVE):{
            rfBoardState = RFBoardReceive;
            break;
        }
        case (ModeSm_StateId_SSB_TRANSMIT):{
            rfBoardState = RFBoardSSBTransmit;
            break;
        }
        case (ModeSm_StateId_CW_TRANSMIT_DIT_MARK):
        case (ModeSm_StateId_CW_TRANSMIT_DAH_MARK):
        case (ModeSm_StateId_CW_TRANSMIT_MARK):{
            rfBoardState = RFBoardCWMark;
            break;
        }
        case (ModeSm_StateId_CW_TRANSMIT_SPACE):
        case (ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE):
        case (ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT):{
            rfBoardState = RFBoardCWSpace;
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
            Debug("Unhandled modeSM.state_id state in UpdateRFBoardState!");
            char strbuf[10];
            sprintf(strbuf, "> %lu",(uint32_t)modeSM.state_id);
            Debug(strbuf);
            break;
        }
    }
    HandleRFBoardStateChange(rfBoardState);
    previousRadioState = modeSM.state_id;
}