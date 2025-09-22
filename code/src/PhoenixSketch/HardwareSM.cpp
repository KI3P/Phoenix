#include "SDT.h"

static ModeSm_StateId previousRadioState = ModeSm_StateId_ROOT;
static RFHardwareState rfHardwareState = RFReceive;
static RFHardwareState oldrfHardwareState = RFinvalid;
static TuneState tuneState = TuneReceive;

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
    oldrfHardwareState = RFTransmit;
    HandleRFHardwareStateChange(RFReceive); // updates oldrfHardwareState
    previousRadioState = ModeSm_StateId_SSB_RECEIVE;
    tuneState = TuneReceive;
    return err;
}

errno_t InitializeRFHardware(void){
    errno_t val = InitializeLPFBoard();
    val += InitializeBPFBoard();
    val += InitializeRFBoard();
    return val;
}

////////////////////////////////////////////////////////////////////////////////////
// State machine code
////////////////////////////////////////////////////////////////////////////////////

ModeSm_StateId GetRFHardwarePreviousState(void){
    return previousRadioState;
}

RFHardwareState GetRFHardwareState(void){
    return rfHardwareState;
}

void HandleRFHardwareStateChange(RFHardwareState newState){
    if (newState == oldrfHardwareState){
        UpdateTuneState();
        return;
    }
    // Following the state diagrams in T41_V12_board_api.drawio, implement the actions
    // required to enter the new state.
    switch (newState){
        case RFReceive:{
            // First, do things that reduce the transmitted power

            // Set cwState to LO
            CWoff();
            // Set clockEnableCW to LO
            DisableCWVFOOutput();
            SetTXAttenuation(31.5);
            TXBypassBPF(); // BPF out of TX path
            SelectXVTR();  // Shunt the TX path to nothing
            Bypass100WPA(); // always bypassed
            // Wait to make sure all the above have happened
            MyDelay(50);
            
            // Now, switch in the receive path hardware
            // Set frequency
            RXSelectBPF(); // BPF in to RX path
            UpdateTuneState();
            // Set GPA state to appropriate value
            SetRXAttenuation( ED.RAtten[ED.currentBand[ED.activeVFO]] );
            // Set driveCurrentSSB_mA to appropriate value
            // > This does not change after initialization, so do nothing
            // Set clockEnableSSB to HI (SSB)
            EnableSSBVFOOutput();
            SelectTXSSBModulation();
            // Make sure calFeedbackState is LO
            DisableCalFeedback();
            // Wait to make sure all these changes have happened
            MyDelay(50);
            // Set rxtxState to LO (RX)
            SelectRXMode();
            // Wait for the relay to switch
            MyDelay(50);
            SetTXAttenuation( ED.XAttenSSB[ED.currentBand[ED.activeVFO]] );
            break;
        }
        case RFTransmit:{
            // Get all the receive hardware out of the path
            RXBypassBPF(); // BPF out of RX path
            // Set calFeedbackState to LO
            DisableCalFeedback();
            MyDelay(50);

            // Start configuring the transmit chain
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
            
            TXSelectBPF(); // BPF in to TX path
            BypassXVTR();  // Bypass XVTR
            Bypass100WPA(); // always bypassed

            MyDelay(50);
            // Set rxtxState to HI (TX)
            SelectTXMode();
            
            break;
        }
        case RFCWMark:{
            // If we come from the RFCWSpace state, we only have to change one thing
            if (oldrfHardwareState != RFCWSpace){
                RXBypassBPF(); // BPF out of RX path
                // Set calFeedbackState to LO
                DisableCalFeedback();
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
                // Set modulationState to LO (CW)
                SelectTXCWModulation();
                TXSelectBPF(); // BPF in to TX path
                BypassXVTR();  // Bypass XVTR
                Bypass100WPA(); // always bypassed
                // Set rxtxState to HI (TX)
                SelectTXMode();
                // Potential issue here if we don't wait for the relay to switch before
                // we turn the CW signal on.
                MyDelay(50);
            }
            // Set cwState to HI
            CWon();
            break;
        }
        case RFCWSpace:{
            // If we come from the RFCWMark state, we only have to change one thing
            if (oldrfHardwareState != RFCWMark){
                RXBypassBPF(); // BPF out of RX path
                // Set calFeedbackState to LO
                DisableCalFeedback();
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
                // Set modulationState to LO (CW)
                SelectTXCWModulation();
                TXSelectBPF(); // BPF in to TX path
                BypassXVTR();  // Bypass XVTR
                Bypass100WPA(); // always bypassed
                // Set rxtxState to HI (TX)
                SelectTXMode();
            }
            // Set cwState to LO
            CWoff();
            break;
        }
        case RFCalIQ:{
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
        case RFinvalid:{
            Debug("Asked to handle RFinvalid state, doing nothing.");
            break;
        }
    }
    oldrfHardwareState = newState;
}

void UpdateRFHardwareState(void){
    if (modeSM.state_id == previousRadioState){
        // Already in this state, no need to change RF hardware, though the
        // tuning might have changed.
        UpdateTuneState();
        return;
    }
    // Several transceiver states map to the same RF board state. Handle this mapping
    // and call the function to handle the new RF board state.
    switch (modeSM.state_id){
        case (ModeSm_StateId_CW_RECEIVE):
        case (ModeSm_StateId_SSB_RECEIVE):{
            rfHardwareState = RFReceive;
            break;
        }
        case (ModeSm_StateId_SSB_TRANSMIT):{
            rfHardwareState = RFTransmit;
            break;
        }
        case (ModeSm_StateId_CW_TRANSMIT_DIT_MARK):
        case (ModeSm_StateId_CW_TRANSMIT_DAH_MARK):
        case (ModeSm_StateId_CW_TRANSMIT_MARK):{
            rfHardwareState = RFCWMark;
            break;
        }
        case (ModeSm_StateId_CW_TRANSMIT_SPACE):
        case (ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE):
        case (ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT):{
            rfHardwareState = RFCWSpace;
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
            Debug("Unhandled modeSM.state_id state in UpdateRFHardwareState!");
            char strbuf[10];
            sprintf(strbuf, "> %lu",(uint32_t)modeSM.state_id);
            Debug(strbuf);
            break;
        }
    }
    HandleRFHardwareStateChange(rfHardwareState);
    previousRadioState = modeSM.state_id;
}


/** Update the center frequency as we switch between CW and SSB and between RX and TX.
 * CW/SSB Receive:  RXfreq = centerFreq_Hz + fineTuneFreq_Hz - SampleRate/4
 * SSB Transmit:    TXfreq = centerFreq_Hz
 * CW Transmit:     TXfreq = centerFreq_Hz + fineTuneFreq_Hz - SampleRate/4 -/+ CWToneOffset
 */
void HandleTuneState(TuneState tuneState){
    SelectLPFBand(ED.currentBand[ED.activeVFO]);
    SelectBPFBand(ED.currentBand[ED.activeVFO]);
    SelectAntenna(ED.antennaSelection[ED.currentBand[ED.activeVFO]]);
    UpdateFIRFilterMask(&filters);
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