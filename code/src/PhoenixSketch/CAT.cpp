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

#include "CAT.h"

// Kenwood TS-480 CAT Interface (partial)
//
// Note that this uses CATSerial for the CAT interface at 38400 baud.
// CATSerial is SerialUSB1 normally, or the primary Serial in builds with
// a USB audio interface, which have only one CDC port (see SDT.h).
// Configure the IDE's Tools->USB Type to Dual Serial, or to Serial + MIDI +
// Audio if you want digital (USB audio) mode.

// Uncomment to see CAT messages on the Serial Output
//#define DEBUG_CAT

bool catTX = false;
static char catCommand[128];
static int catCommandIndex = 0;
static char obuf[256];

// Stupid compiler warnings....
const char empty_string[1] = {""};
char *empty_string_p = (char*)&empty_string[0];

char *command_parser( char* command );

char* unsupported_cmd( char* cmd  );
char* AG_read(  char* cmd );
char* AG_write( char* cmd );
char* AI_read(  char* cmd );
char* AI_write( char* cmd );
char *BU_write( char* cmd );
char *BD_write( char* cmd );
char *DB_write( char* cmd );
char *FA_write( char* cmd );
char *FA_read(  char* cmd );
char *FB_write( char* cmd );
char *FB_read(  char* cmd );
char *FT_write( char* cmd );
char *FT_read(  char* cmd );
char *FR_write( char* cmd );
char *FR_read(  char* cmd );
char *FW_write( char* cmd );
char *FW_read(  char* cmd );
char *ID_read(  char* cmd );
char *IF_read(  char* cmd );
char *KS_write( char* cmd );
char *KS_read(  char* cmd );
char *MD_write( char* cmd );
char *MD_read(  char* cmd );
char *MG_write( char* cmd );
char *MG_read(  char* cmd );
char *NR_write( char* cmd );
char *NR_read(  char* cmd );
char *NT_write( char* cmd );
char *NT_read(  char* cmd );
char *PC_write( char* cmd );
char *PC_read(  char* cmd );
char *PD_read(  char* cmd );
char *PS_write( char* cmd );
char *PS_read(  char* cmd );
char *RX_write( char* cmd );
char *TX_write( char* cmd );
char *VX_write( char* cmd );
char *VX_read( char* cmd );
char *ED_read(  char* cmd );
char *PR_read( char* cmd);
char *SR_write( char* cmd );
char *SR_read(  char* cmd );
char *DG_write( char* cmd );
char *DG_read(  char* cmd );
char *DR_write( char* cmd );
char *DR_read(  char* cmd );
char *DS_read(  char* cmd );
char *CF_write( char* cmd );
char *CF_read(  char* cmd );
char *EQ_write( char* cmd );
char *EQ_read(  char* cmd );
char *FL_write( char* cmd );
char *FL_read(  char* cmd );

typedef struct  {
    char name[3];   //two chars plus zero terminator
    int set_len;
    int read_len;
    char* (*write_function)( char* );  //pointer to write function. Takes a pointer to the command packet, and its length. Returns result as char*
    char* (*read_function)(  char* );  //pointer to read function. Takes a pointer to the command packet, and its length. Returns?
} valid_command;

// The command_parser will compare the CAT command received against the entires in
// this array. If it matches, then it will call the corresponding write_function
// or the read_function, depending on the length of the command string.
// Note on set_len / read_len: command_parser() tests the write form FIRST, so a
// command whose two lengths are equal can never reach its read function. Keep
// them distinct for anything that needs to be readable.
//
// Note on optional parameters: a command whose parameter the spec allows to be
// omitted is expressed by putting the *write* function in BOTH slots, with
// set_len covering the parameterised form and read_len the bare one. TX and RX
// below are the only such commands. Do not "fix" them by writing a read handler.
#define NUM_SUPPORTED_COMMANDS 32
valid_command valid_commands[ NUM_SUPPORTED_COMMANDS ] =
    {
        { "AG", 3+4,4, AG_write, AG_read },  //audio gain
        { "AI", 3+1,3, AI_write, AI_read },  //auto information
        { "BD", 3,  0, BD_write, unsupported_cmd }, //band down, no read, only set
        { "BU", 3,  0, BU_write, unsupported_cmd }, //band up
        { "DB", 3+4,3, DB_write, unsupported_cmd }, //dBm calibration
        { "FA", 3+11,3, FA_write, FA_read },  //VFO A
        { "FB", 3+11,3, FB_write, FB_read },  //VFO B
        { "FR", 3+1, 3, FR_write, FR_read }, // selects or reads the VFO of the receiver
        { "FT", 3+1, 3, FT_write, FT_read }, // selects or reads the VFO of the transmitter
        { "FW", 3+4,3,  FW_write, FW_read }, // DSP filter bandwidth (high cut)
        { "ID", 0,  3, unsupported_cmd, ID_read }, // RADIO ID#, read-only
        { "IF", 0,  3, unsupported_cmd, IF_read }, //radio status, read-only
        { "KS", 3+1,3, KS_write, KS_read }, // keyer speed
        { "MD", 3+1,3, MD_write, MD_read }, // operating mode, CW, USB etc
        { "MG", 3+3,3, MG_write, MG_read }, // mike gain
        { "NR", 3+1,3, NR_write, NR_read }, // Noise reduction function: 0=off
        { "NT", 4,  3, NT_write, NT_read }, // Auto Notch 0=off, 1=ON -- NOT a Kenwood keyword
        { "PC", 3+3,3, PC_write, PC_read }, // output power
        { "PD", 0,  3, unsupported_cmd, PD_read }, // read the PSD -- NOT a Kenwood keyword
        { "PS", 3+1,3, PS_write, PS_read },  // Rig power on/off
        // RX; unkeys; RX0;/RX1; (main/sub receiver) are accepted too and behave
        // identically - Phoenix has one receiver. Both forms are writes.
        { "RX", 3+1,3, RX_write, RX_write },  // Receiver function 0=main 1=sub
        // TX; keys, and so do TX0; (normal/MIC), TX1; (DTS via ANI input) and
        // TX2; (TX Tune): ts_480_pc.pdf p.21 makes P1 optional and defaults it
        // to 0. Both forms are writes - a bare TX; is a set, not a read - and
        // TX_write ignores P1, so every form keys identically. Hamlib picks
        // between them by PTT type (TX; / TX0; / TX1;), so a client that only
        // sends one of them must not be turned away.
        { "TX", 3+1,3, TX_write, TX_write }, // set transceiver to transmit.
        { "VX", 3+1, 3, VX_write, VX_read }, // VOX write/read
        { "ED", 0,  3, unsupported_cmd, ED_read }, // print out the state of the EEPROM data -- NOT a Kenwood keyword
        { "PR", 0,  3, unsupported_cmd, PR_read }, // print out the state of the hardware register -- NOT a Kenwood keyword
        { "SR", 3+1,3, SR_write, SR_read }, // sample rate 0=176.4k 1=192k -- NOT a Kenwood keyword
        { "CF", 3+1,3, CF_write, CF_read }, // receive CW audio filter index -- NOT a Kenwood keyword
        { "EQ", 3+5,3+2, EQ_write, EQ_read }, // receive equalizer cell -- NOT a Kenwood keyword
        { "FL", 3+4,3, FL_write, FL_read }, // DSP filter low cut -- NOT a Kenwood keyword
        { "DG", 3+1,3, DG_write, DG_read }, // digital (USB audio) mode 0=off 1=on -- NOT a Kenwood keyword
        { "DR", 3+3,3, DR_write, DR_read }, // digital receive level 000-100 -- NOT a Kenwood keyword
        { "DS", 0,  3, unsupported_cmd, DS_read } // digital TX path stats, read-only -- NOT a Kenwood keyword
    };

/**
 * Handler for unsupported CAT commands
 * @param cmd The CAT command string
 * @return Error response "?;" indicating unsupported command
 */
char *unsupported_cmd( char *cmd ){
    sprintf( obuf, "?;");
    return obuf;
}

/**
 * Return the audio volume contained in the EEPROMData->audioVolume variable
 */
char *AG_read(  char* cmd ){
    sprintf( obuf, "AG%c%03ld;", cmd[ 2 ], ( int32_t )( ( ( float32_t )ED.audioVolume * 255.0 ) / 100.0 ) );
    return obuf;
}

/**
 * Set the audio volume to the passed paramter, scaling before doing so
 */
char *AG_write( char* cmd  ){
    ED.audioVolume = ( int32_t )( ( ( float32_t )atoi( &cmd[3] ) * 100.0 ) / 255.0 );
    if( ED.audioVolume > 100 ) ED.audioVolume = 100;
    if( ED.audioVolume < 0 ) ED.audioVolume = 0;
    return empty_string_p;
}


/**
 * Return AI0. This command exists only for compatability with hamlib expectations
 * for a TS-480 radio -- it does nothing.
 */
char *AI_read(  char* cmd ){
    sprintf( obuf, "AI0;");
    return obuf;
}

/**
 * This command exists only for compatability with hamlib expectations
 * for a TS-480 radio -- it does nothing.
 */
char *AI_write( char* cmd  ){
    return empty_string_p;
}

/**
 * Change up one band by simulating the band up button being pressed
 */
char *BU_write( char* cmd  ){
    SetButton(BAND_UP);
    SetInterrupt(iBUTTON_PRESSED);
    return empty_string_p;
}

/**
 * Change down one band by simulating the band down button being pressed
 */
char *BD_write( char* cmd ){
    SetButton(BAND_DN);
    SetInterrupt(iBUTTON_PRESSED);
    return empty_string_p;
}

/**
 * Set the dBm calibration value
 * @param cmd CAT command containing calibration value after position 2
 * @return Empty string
 */
char *DB_write( char* cmd  ){
    ED.dbm_calibration[ED.currentBand[ED.activeVFO]] = ( float32_t ) atof( &cmd[2] );
    Debug(ED.dbm_calibration[ED.currentBand[ED.activeVFO]]);
    return empty_string_p;
}

void AdjustBand(void); // in Loop.cpp
/**
 * Set VFO frequency and save previous frequency to lastFrequencies array
 * @param freq Frequency in Hz to set
 * @param vfo VFO number (VFO_A or VFO_B)
 *
 * Saves current frequency settings, determines new band, and updates VFO parameters.
 * Triggers tune update interrupt after setting new frequency.
 */
void set_vfo(int64_t freq, uint8_t vfo){
    // Save the current VFO settings to the lastFrequencies array
    // lastFrequencies is [NUMBER_OF_BANDS][2]
    // the current band for VFO A is ED.currentBand[0], B is ED.currentBand[1]
    ED.lastFrequencies[ED.currentBand[vfo]][0] = ED.centerFreq_Hz[vfo];
    ED.lastFrequencies[ED.currentBand[vfo]][1] = ED.fineTuneFreq_Hz[vfo];
    int newband = GetBand(freq);
    if (newband != -1){
        ED.currentBand[vfo] = newband;
    }
    // Set the frequencies
    ED.centerFreq_Hz[vfo] = freq + SR[SampleRate].rate/4;
    ED.fineTuneFreq_Hz[vfo] = 0;
    AdjustBand();
    SetInterrupt(iUPDATE_TUNE);
}

/**
 * Set VFO A frequency
 * @param freq Frequency in Hz to set for VFO A
 */
void set_vfo_a( long freq ){
    set_vfo(freq, VFO_A);
}

/**
 * Set VFO B frequency
 * @param freq Frequency in Hz to set for VFO B
 */
void set_vfo_b( long freq ){
    set_vfo(freq, VFO_B);
}

/**
 * CAT command FA - Set VFO A frequency
 * @param cmd CAT command string with frequency after position 2
 * @return Response string with set frequency
 */
char *FA_write( char* cmd ){
    long freq = atol( &cmd[ 2 ] );
    set_vfo_a( freq );
    sprintf( obuf, "FA%011ld;", freq );
    return obuf;
}

/**
 * CAT command FA - Read VFO A frequency
 * @param cmd CAT command string
 * @return Response string with current VFO A frequency
 */
char *FA_read(  char* cmd  ){
    sprintf( obuf, "FA%011lld;", GetTXRXFreq(VFO_A) );
    return obuf;
}

/**
 * CAT command FB - Set VFO B frequency
 * @param cmd CAT command string with frequency after position 2
 * @return Response string with set frequency
 */
char *FB_write( char* cmd  ){
    long freq = atol( &cmd[ 2 ] );
    set_vfo_a( freq ); // was set vfo_a
    sprintf( obuf, "FB%011ld;", freq );
    return obuf;
}

/**
 * CAT command FB - Read VFO B frequency
 * @param cmd CAT command string
 * @return Response string with current VFO B frequency
 */
char *FB_read(  char* cmd  ){
    sprintf( obuf, "FB%011lld;", GetTXRXFreq(VFO_B) );
    return obuf;
}

/**
 * CAT command FT - Set transmit frequency (assumes no SPLIT operation)
 * @param cmd CAT command string with frequency after position 2
 * @return Response string with set frequency
 */
char *FT_write( char* cmd  ){
    int vfo = atol( &cmd[ 2 ] );
    if ((vfo >= 0) && (vfo <=1)){
        ED.activeVFO = vfo;
        sprintf( obuf, "FT%d;", ED.activeVFO);
    } else {
        sprintf( obuf, "?;");
    }
    return obuf;
}

/**
 * CAT command FT - Read transmit frequency (assumes no SPLIT operation)
 * @param cmd CAT command string
 * @return Response string with current transmit frequency
 */
char *FT_read(  char* cmd  ){
    sprintf( obuf, "FT%d;", ED.activeVFO);
    return obuf;
}

/**
 * CAT command FR - Set receive frequency (assumes no SPLIT operation)
 * @param cmd CAT command string with VFO number after position 2
 * @return Response string with active VFO number 
 */
char *FR_write( char* cmd  ){
    int vfo = atol( &cmd[ 2 ] );
    if ((vfo >= 0) && (vfo <=1)){
        ED.activeVFO = vfo;
        sprintf( obuf, "FR%d;", ED.activeVFO);
    } else {
        sprintf( obuf, "?;");
    }
    return obuf;
}

/**
 * CAT command FR - Read active VFO
 * @param cmd CAT command string
 * @return Response string with active VFO
 */
char *FR_read(  char* cmd  ){
    sprintf( obuf, "FR%d;", ED.activeVFO);
    return obuf;
}

/**
 * Return the DSP filter bandwidth in the form FWADCD; where ABCD is bandwidth in Hz.
 * We return the upper frequency.
 */
char *FW_read(  char* cmd ){
    int32_t fhigh = 0;
    switch (bands[ED.currentBand[ED.activeVFO]].mode) {
        case LSB:{
            fhigh = -bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz ;
            break;
        }
        case AM:
        case SAM:
        case USB:{
            fhigh = bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
            break;
        }
        case IQ:
        case DCF77:
            break;
    }
    sprintf( obuf, "FW%04ld;", fhigh);
    return obuf;
}

/**
 * Set the filter bandwidth.
 */
char *FW_write( char* cmd  ){
    int32_t g = atoi( &cmd[2] );
    switch (bands[ED.currentBand[ED.activeVFO]].mode) {
        case LSB:{
            if (g > -bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz)
                bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz = -g;
            break;
        }
        case AM:
        case SAM:
        case USB:{
            if (g > bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz)
                bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz = g;
            break;
        }
        case IQ:
        case DCF77:
            break;
    }
    // Calculate the new FIR filter mask
    UpdateFIRFilterMask(&RXfilters);
    return empty_string_p;
}

/**
 * CAT command ID - Read radio identification
 * @param cmd CAT command string
 * @return Response "ID020;" (Kenwood TS-480 identifier)
 */
char *ID_read(  char* cmd  ){
    sprintf( obuf, "ID020;");
    return obuf;                            // Kenwood TS-480
}

/**
 * CAT command IF - Read complete radio status information
 * @param cmd CAT command string
 * @return Response string with frequency, mode, RX/TX status, and other parameters
 */
char *IF_read(  char* cmd ){
    int mode;
    if (( modeSM.state_id == ModeSm_StateId_CW_RECEIVE ) | 
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_DAH_MARK ) |
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_DIT_MARK ) |
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE ) |
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT ) |
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_MARK ) |
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_SPACE )
        ){
        mode = 3;
    }else{
        switch( ED.modulation[ED.activeVFO] ){   // current modulation, not the band default
            case LSB:
                mode = 1; // LSB
                break;
            case USB:
                mode = 2; // USB
                break;
            case AM:
            case SAM:
                mode = 5; // AM
                break;
            default:
                mode = 1; // LSB
                break;
        }
    }
    uint8_t rxtx;
    if ((modeSM.state_id == ModeSm_StateId_CW_RECEIVE) |
        (modeSM.state_id == ModeSm_StateId_DIGITAL_RECEIVE) |
        (modeSM.state_id == ModeSm_StateId_SSB_RECEIVE)){
        rxtx = 0;
    } else {
        rxtx = 1;
    }
    
    sprintf( obuf,
            //  P1     P2   P3   P4P5P6P7  P8P9P10 P12 P14 P15
            //                                   P11 P13
             "IF%011lld     %+05d%d%d%d%02d%d%d%d%d%d%d%02d ;",
             GetTXRXFreq(ED.activeVFO),  // P1: frequency
             // P2 is 5 spaces
             0, // P3: rit/xit frequency
             0, // P4: rit enabled
             0, // P5: xit enabled
             0, // P6: always 0, Channel bank
             0, // P7: memory channel number
             rxtx, // P8: RX/TX (always RX in test)
             mode, // P9: operating mode
             ED.activeVFO, // P10: active VFO
             0, // P11: Scan Status
             0, // P12: split
             0, // P13: CTCSS enabled (OFF)
             0  // P14: tone number
             // P15 is a space character
              );
    return obuf;
}

/**
 * Return the keyer speed in the form KSABC; where ABC is speed in WPM.
 */
char *KS_read(  char* cmd ){
    sprintf( obuf, "KS%03ld;", ED.currentWPM);
    return obuf;
}

/**
 * Set the keyer speed.
 */
char *KS_write( char* cmd  ){
    int32_t g = atoi( &cmd[2] );
    if ((g >= 10) && (g <= 60)) // limits specified by the TS-480 manual
        ED.currentWPM = g;
    return empty_string_p;
}

/**
 * Select the demodulator.
 *
 * Writes ED.modulation[] and nothing else. bands[].mode is the band's *default*
 * modulation and must stay put: InitFilterMask() mirrors the band's stored
 * passband whenever the current modulation differs from that default, and that
 * mirroring is the only thing that produces the opposite sideband.
 *
 * Writing bands[].mode here used to look like "keeping the two copies in step",
 * but it is what selected the wrong sideband. Setting the default equal to the
 * newly requested modulation removes the disagreement, so the mirroring stops and
 * the band-default passband comes back. On 40 m (default LSB) `MD2;` - select USB
 * - therefore switched the radio to LSB, while the display, which reads
 * ED.modulation[], went on saying USB. This now matches the front panel's
 * DEMODULATION button, which has always written ED.modulation[] alone.
 *
 * Also leaves CW receive if that is where we are: the mode state machine has no
 * other CAT-reachable path back to SSB.
 *
 * Digital mode is deliberately NOT left here. WSJT-X and similar clients send
 * `MD2;` to force USB on startup and before every transmission, so treating a
 * modulation change as a request to leave digital mode would make the mode
 * impossible to hold. Use `DG0;` or the front panel MODE button to leave it.
 *
 * @param mode The modulation to select
 */
static void SetModulation(ModulationType mode){
    if (modeSM.state_id == ModeSm_StateId_CW_RECEIVE){
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_SSB_MODE);
    }
    ED.modulation[ED.activeVFO] = mode;
    SetInterrupt(iMODE);
}

/**
 * CAT command MD - Set operating mode
 * @param cmd CAT command string with mode number: 1=LSB, 2=USB, 3=CW, 4=AM, 5=SAM
 * @return Empty string
 */
char *MD_write( char* cmd  ){
    int p1 = atoi( &cmd[2] );
    switch( p1 ){
        case 1: // LSB
            SetModulation(LSB);
            break;
        case 2: // USB
            SetModulation(USB);
            break;

        case 3: // CW
            // Change to CW mode if in a non-CW receive mode, otherwise ignore:
            if ((modeSM.state_id == ModeSm_StateId_SSB_RECEIVE) ||
                (modeSM.state_id == ModeSm_StateId_DIGITAL_RECEIVE)){
                if( ED.currentBand[ED.activeVFO] < BAND_30M ){
                    SetModulation(LSB);
                }else{
                    SetModulation(USB);
                }
                ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
            }
            break;
        case 4: // AM
            SetModulation(AM);
            break;
        case 5: // SAM
            SetModulation(SAM);
            break;
        default:
            break;
    }
    return empty_string_p;
}

/**
 * CAT command MD - Read current operating mode
 * @param cmd CAT command string
 * @return Response string with mode: MD1 (LSB), MD2 (USB), MD3 (CW), MD5 (AM/SAM)
 */
char *MD_read( char* cmd ){
    if( ( modeSM.state_id == ModeSm_StateId_CW_RECEIVE ) | 
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_DAH_MARK ) |
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_DIT_MARK ) |
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE ) |
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT ) |
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_MARK ) |
        ( modeSM.state_id == ModeSm_StateId_CW_TRANSMIT_SPACE ) ){ sprintf( obuf, "MD3;" ); return obuf; }
    // ED.modulation[] is the current modulation; bands[].mode is only the band
    // default, and reporting that told CAT clients the wrong sideband whenever
    // the operator had selected the non-default one.
    if( ED.modulation[ED.activeVFO] == LSB ){ sprintf( obuf, "MD1;" ); return obuf; }
    if( ED.modulation[ED.activeVFO] == USB ){ sprintf( obuf, "MD2;" ); return obuf; }
    if( ED.modulation[ED.activeVFO] == AM  ){ sprintf( obuf, "MD4;" ); return obuf; }
    if( ED.modulation[ED.activeVFO] == SAM ){ sprintf( obuf, "MD5;" ); return obuf; }
    sprintf( obuf, "?;");
    return obuf;  //Huh? How'd we get here?
}

/**
 * CAT command MG - Set microphone gain
 * @param cmd CAT command string with gain value (0-100, converted to -40 to +30 dB)
 * @return Empty string
 */
char *MG_write( char* cmd ){
    int g = atoi( &cmd[2] );
    // convert from 0..100 to -40..30
    g = ( int )( ( ( double )g * 70.0 / 100.0 ) - 40.0 );
    ED.currentMicGain = g;
    if( modeSM.state_id == ModeSm_StateId_SSB_TRANSMIT ){
        // we're actively transmitting, increase gain without interrupting transmit
        UpdateTransmitAudioGain();
    }
    return empty_string_p;
}

/**
 * CAT command MG - Read microphone gain
 * @param cmd CAT command string
 * @return Response string with gain value (0-100, converted from -40 to +30 dB)
 */
char *MG_read(  char* cmd ){
    // convert from -40 .. 30 to 0..100
    int g = ( int )( ( double )( ED.currentMicGain + 40 ) * 100.0 / 70.0 );
    sprintf( obuf, "MG%03d;", g );
    return obuf;
}

/**
 * CAT command NR - Set noise reduction mode
 * @param cmd CAT command string with NR mode (0=off, other values select NR type)
 * @return Empty string
 */
char *NR_write( char* cmd ){
    if( cmd[ 2 ] == '0' ){
        ED.nrOptionSelect = NROff;
    }else{
        ED.nrOptionSelect = (NoiseReductionType) atoi( &cmd[2] );
    }
    return empty_string_p;
}

/**
 * CAT command NR - Read noise reduction mode
 * @param cmd CAT command string
 * @return Response string with current NR mode
 */
char *NR_read(  char* cmd ){
    sprintf( obuf, "NR%d;", ED.nrOptionSelect );
    return obuf;
}

/**
 * CAT command NT - Set auto-notch filter on/off
 * @param cmd CAT command string with value (0=off, 1=on)
 * @return Empty string
 */
char *NT_write( char* cmd ){
    uint8_t v = atoi( &cmd[2] );
    if (v < 2){
        ED.ANR_notchOn = v;
    }
    return empty_string_p;
}

/**
 * CAT command NT - Read auto-notch filter status
 * @param cmd CAT command string
 * @return Empty string
 */
char *NT_read(  char* cmd ){
    return empty_string_p;
}

/**
 * CAT command PC - Set output power level
 * @param cmd CAT command string with power value (mode-specific: SSB or CW)
 * @return Response string with set power value
 */
char *PC_write( char* cmd ){
    int requested_power = atoi( &cmd[ 3 ]);
    if( ( modeSM.state_id == ModeSm_StateId_SSB_RECEIVE ) |
        ( modeSM.state_id == ModeSm_StateId_SSB_TRANSMIT ) ){
        ED.powerOutSSB[ED.currentBand[ED.activeVFO]] = requested_power;
    } else {
        ED.powerOutCW[ED.currentBand[ED.activeVFO]] = requested_power;
    }
    SetInterrupt(iPOWER_CHANGE);
    sprintf( obuf, "PC%03d;", requested_power );
    return obuf;
}

/**
 * CAT command PC - Read output power level
 * @param cmd CAT command string
 * @return Response string with current power value (mode-specific: SSB or CW)
 */
char *PC_read(  char* cmd ){
    unsigned int o_param;
    if( ( modeSM.state_id == ModeSm_StateId_SSB_RECEIVE ) |
        ( modeSM.state_id == ModeSm_StateId_SSB_TRANSMIT ) ){
        o_param = round( ED.powerOutSSB[ED.currentBand[ED.activeVFO]] );
    } else {
        o_param = round( ED.powerOutCW[ED.currentBand[ED.activeVFO]] );
    }
    sprintf( obuf, "PC%03d;", o_param );
    return obuf;
}

/**
 * CAT command PD - Read Power Spectral Density data (non-standard Kenwood command)
 * @param cmd CAT command string
 * @return Response string "PD;" after printing all PSD values to Serial
 */
char *PD_read(  char* cmd ){
    for (int j = 0; j < SPECTRUM_RES; j++){
        sprintf( obuf, "%d,%4.3f", j, psdnew[j] );
        Serial.println(obuf);
    }
    sprintf( obuf, "PD;");
    return obuf;
}

/**
 * CAT command PS - Turn radio power off
 * @param cmd CAT command string
 * @return Response "PS0;" (requests shutdown from ATtiny)
 */
char *PS_write( char* cmd ){
    // Ask the AtTiny to do it
    ShutdownTeensy();
    sprintf( obuf, "PS0;");
    return obuf;    //Nope.  Not doing that.
}

/**
 * CAT command PS - Read power status
 * @param cmd CAT command string
 * @return Response "PS1;" (power is on - if we're responding, power must be on)
 */
char *PS_read(  char* cmd ){
    sprintf( obuf, "PS1;");
    return obuf;          // The power's on.  Otherwise, we're not answering!
}

/**
 * CAT command RX - Switch to receive mode
 * @param cmd CAT command string
 * @return Response "RX0;" after releasing PTT or key depending on current mode
 */
char *RX_write( char* cmd ){
    switch (modeSM.state_id){
        case (ModeSm_StateId_DIGITAL_TRANSMIT):
        case (ModeSm_StateId_SSB_TRANSMIT):{
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
            break;
        } 
        case (ModeSm_StateId_CW_TRANSMIT_MARK):{
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_RELEASED);
            break;
        }
        default:
            break;
    }
    return empty_string_p;
}

/**
 * CAT command TX - Switch to transmit mode
 * @param cmd CAT command string
 * @return Response "TX0;" after pressing PTT or key depending on current mode
 */
char *TX_write( char* cmd ){
    switch (modeSM.state_id){
        case (ModeSm_StateId_DIGITAL_RECEIVE):
        case (ModeSm_StateId_SSB_RECEIVE):{
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
            break;
        } 
        case (ModeSm_StateId_CW_RECEIVE):{
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_PRESSED);
            break;
        }
        default:
            break;
    }
    return empty_string_p;
}

char *VX_write( char* cmd ){
    Debug("Got VX write");
    return empty_string_p;
}

char *VX_read( char* cmd ){
    Debug("Got VX read");
    sprintf( obuf, "VX0;");
    return obuf;
}

/**
 * CAT command ED - Dump EEPROM data structure to Serial (non-standard Kenwood command)
 * @param cmd CAT command string
 * @return Response "ED;" after printing all ED struct contents to Serial for debugging
 */
char *ED_read(  char* cmd  ){
    // Print out the state of the EEPROM data
    PrintEDToSerial();
    sprintf( obuf, "ED;");
    return obuf;
}

/**
 * CAT command PR - Pretty print the contents of the hardware register (non-standard Kenwood command)
 * @param cmd CAT command string
 * @return Response "PR;" after printing hardware register to Serial for debugging
 */
char *PR_read(  char* cmd  ){
    buffer_pretty_print_last_entry();
    sprintf( obuf, "PR;");
    return obuf;
}

/** Sample rates reachable over CAT, in the order the SR parameter selects them.
 *  These are the two rates the front panel menu offers. */
static const uint8_t SR_CAT_RATES[2] = { SAMPLE_RATE_176K, SAMPLE_RATE_192K };
#define SR_CAT_RATE_COUNT 2

/**
 * CAT command SR - set the sample rate (non-standard Kenwood command)
 *
 * `SR0;` selects 176.4 ksps, `SR1;` selects 192 ksps. Rejected while transmitting,
 * because ChangeSampleRate() reconfigures the I2S clock and rebuilds the whole
 * DSP chain.
 *
 * @param cmd CAT command string
 * @return Empty string on success, "?;" if the rate is out of range or the radio is not receiving
 */
char *SR_write( char* cmd ){
    int p1 = cmd[2] - '0';
    if( p1 < 0 || p1 >= SR_CAT_RATE_COUNT ){
        sprintf( obuf, "?;");
        return obuf;
    }
    // Only while receiving, and never in digital mode: that mode pins the radio
    // at 176.4 ksps so the USB audio endpoint's 44.1 kHz needs no resampling.
    if( modeSM.state_id != ModeSm_StateId_SSB_RECEIVE &&
        modeSM.state_id != ModeSm_StateId_CW_RECEIVE ){
        sprintf( obuf, "?;");
        return obuf;
    }
    ChangeSampleRate( SR_CAT_RATES[ p1 ] );
    return empty_string_p;
}

/**
 * CAT command SR - read the sample rate (non-standard Kenwood command)
 * @param cmd CAT command string
 * @return Response "SRn;" where n indexes SR_CAT_RATES, or "?;" if the radio is at some other rate
 */
char *SR_read( char* cmd ){
    for( int i = 0; i < SR_CAT_RATE_COUNT; i++ ){
        if( SampleRate == SR_CAT_RATES[ i ] ){
            sprintf( obuf, "SR%d;", i);
            return obuf;
        }
    }
    sprintf( obuf, "?;");
    return obuf;
}

/**
 * CAT command DG - enter or leave digital (USB audio) mode (non-standard Kenwood command)
 *
 * `DG1;` enters digital mode, `DG0;` returns to SSB. Entering forces the radio to
 * 176.4 ksps and starts the USB audio link; leaving restores the previous rate.
 *
 * Only accepted from a receive state, because the mode change reconfigures the
 * I2S clock and rebuilds the DSP chain. Rejected outright in builds without a
 * USB audio interface, where digital mode has no transport.
 *
 * @param cmd CAT command string
 * @return Empty string on success, "?;" if the parameter is out of range, the
 *         radio is not receiving, or USB audio is not compiled in
 */
char *DG_write( char* cmd ){
    int p1 = cmd[2] - '0';
    if( p1 < 0 || p1 > 1 ){
        sprintf( obuf, "?;");
        return obuf;
    }
#ifndef AUDIO_INTERFACE
    sprintf( obuf, "?;");
    return obuf;
#else
    if( p1 == 1 ){
        if( modeSM.state_id != ModeSm_StateId_SSB_RECEIVE &&
            modeSM.state_id != ModeSm_StateId_CW_RECEIVE &&
            modeSM.state_id != ModeSm_StateId_DIGITAL_RECEIVE ){
            sprintf( obuf, "?;");
            return obuf;
        }
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    } else {
        if( modeSM.state_id != ModeSm_StateId_DIGITAL_RECEIVE ){
            // Already out of digital mode, or transmitting - nothing to do
            if( modeSM.state_id == ModeSm_StateId_DIGITAL_TRANSMIT ){
                sprintf( obuf, "?;");
                return obuf;
            }
            return empty_string_p;
        }
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_SSB_MODE);
    }
    UpdateRFHardwareState();
    return empty_string_p;
#endif
}

/**
 * CAT command DG - read whether digital (USB audio) mode is active
 * @param cmd CAT command string
 * @return Response "DG1;" in digital mode, "DG0;" otherwise
 */
char *DG_read( char* cmd ){
    if( modeSM.state_id == ModeSm_StateId_DIGITAL_RECEIVE ||
        modeSM.state_id == ModeSm_StateId_DIGITAL_TRANSMIT ){
        sprintf( obuf, "DG1;");
    } else {
        sprintf( obuf, "DG0;");
    }
    return obuf;
}

/**
 * CAT command DR - set the digital-mode receive level (non-standard Kenwood command)
 *
 * `DR000;` to `DR100;` scale the audio sent to the host over USB. The receive
 * chain does not regulate this level for us - with AGC off the AGC stage applies
 * a fixed gain of 20 - so a strong signal will otherwise reach full scale and
 * clip. The default of 25 is -12 dB.
 *
 * @param cmd CAT command string
 * @return Empty string on success, "?;" if the level is out of range
 */
char *DR_write( char* cmd ){
    int32_t level = atoi( &cmd[2] );
    if( level < 0 || level > 100 ){
        sprintf( obuf, "?;");
        return obuf;
    }
    ED.digitalRxLevel = level;
    return empty_string_p;
}

/**
 * CAT command DR - read the digital-mode receive level (non-standard Kenwood command)
 * @param cmd CAT command string
 * @return Response "DRnnn;" with the level from 000 to 100
 */
char *DR_read( char* cmd ){
    sprintf( obuf, "DR%03ld;", (long)ED.digitalRxLevel);
    return obuf;
}

/**
 * CAT command DS - read the digital transmit path health counters (non-standard)
 *
 * Returns "DS<calls>,<underruns>,<trimmed>,<depthMin>,<depthMax>;". The counters
 * reset when digital mode is entered. Both an underrun and a trim splice
 * discontinuous audio into the transmit stream, which lands on the air as
 * sidebands at the 128-sample block rate.
 *
 * @param cmd CAT command string
 * @return Response with the five counters
 */
char *DS_read( char* cmd ){
    uint32_t calls=0, under=0, trim=0;
    uint8_t dmin=0, dmax=0;
    USBAudioGetTxStats(&calls, &under, &trim, &dmin, &dmax);
    sprintf( obuf, "DS%lu,%lu,%lu,%u,%u;", (unsigned long)calls, (unsigned long)under,
             (unsigned long)trim, (unsigned)dmin, (unsigned)dmax);
    return obuf;
}

/**
 * CAT command CF - select the receive CW audio filter (non-standard Kenwood command)
 *
 * `CF0;` through `CF4;` select the five CW bandwidths (840, 1080, 1320, 1800 and
 * 2000 Hz); `CF5;` bypasses the filter.
 *
 * @param cmd CAT command string
 * @return Empty string on success, "?;" if the index is out of range
 */
char *CF_write( char* cmd ){
    int p1 = cmd[2] - '0';
    if( p1 < 0 || p1 > 5 ){
        sprintf( obuf, "?;");
        return obuf;
    }
    ED.CWFilterIndex = p1;
    return empty_string_p;
}

/**
 * CAT command CF - read the receive CW audio filter index (non-standard Kenwood command)
 * @param cmd CAT command string
 * @return Response "CFn;"
 */
char *CF_read( char* cmd ){
    sprintf( obuf, "CF%ld;", (long)ED.CWFilterIndex);
    return obuf;
}

/**
 * CAT command EQ - set one receive equalizer cell (non-standard Kenwood command)
 *
 * `EQbbvvv;` where bb is the cell index 00..13 and vvv is the level 000..100.
 *
 * @param cmd CAT command string
 * @return Empty string on success, "?;" if the cell index or level is out of range
 */
char *EQ_write( char* cmd ){
    char idxbuf[3] = { cmd[2], cmd[3], '\0' };
    int band = atoi( idxbuf );
    int value = atoi( &cmd[4] );
    if( band < 0 || band >= EQUALIZER_CELL_COUNT || value < 0 || value > 100 ){
        sprintf( obuf, "?;");
        return obuf;
    }
    ED.equalizerRec[ band ] = value;
    return empty_string_p;
}

/**
 * CAT command EQ - read one receive equalizer cell (non-standard Kenwood command)
 * @param cmd CAT command string
 * @return Response "EQbbvvv;", or "?;" if the cell index is out of range
 */
char *EQ_read( char* cmd ){
    char idxbuf[3] = { cmd[2], cmd[3], '\0' };
    int band = atoi( idxbuf );
    if( band < 0 || band >= EQUALIZER_CELL_COUNT ){
        sprintf( obuf, "?;");
        return obuf;
    }
    sprintf( obuf, "EQ%02d%03ld;", band, (long)ED.equalizerRec[ band ]);
    return obuf;
}

/**
 * CAT command FL - set the DSP filter low cut (non-standard Kenwood command)
 *
 * The mirror of FW, which moves the other edge: on USB/AM/SAM this moves
 * FLoCut_Hz, on LSB it moves FHiCut_Hz. The value is always the magnitude of the
 * edge frequency, as it is for FW.
 *
 * @param cmd CAT command string
 * @return Empty string
 */
char *FL_write( char* cmd ){
    int32_t g = atoi( &cmd[2] );
    switch (bands[ED.currentBand[ED.activeVFO]].mode) {
        case LSB:{
            // On LSB the passband is negative, so the low cut is the high edge.
            if (g < -bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz)
                bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz = -g;
            break;
        }
        case AM:
        case SAM:
        case USB:{
            if (g < bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz)
                bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz = g;
            break;
        }
        case IQ:
        case DCF77:
            break;
    }
    UpdateFIRFilterMask(&RXfilters);
    return empty_string_p;
}

/**
 * CAT command FL - read the DSP filter low cut (non-standard Kenwood command)
 * @param cmd CAT command string
 * @return Response "FLnnnn;"
 */
char *FL_read( char* cmd ){
    int32_t flow = 0;
    switch (bands[ED.currentBand[ED.activeVFO]].mode) {
        case LSB:{
            flow = -bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
            break;
        }
        case AM:
        case SAM:
        case USB:{
            flow = bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz;
            break;
        }
        case IQ:
        case DCF77:
            break;
    }
    sprintf( obuf, "FL%04ld;", (long)flow);
    return obuf;
}


/**
 * Poll the CAT serial port for incoming CAT commands and process them
 *
 * Reads characters from the CAT serial port, buffers them until a semicolon
 * terminator is received, then parses and executes the command via command_parser().
 * Sends response back over the same port. Handles buffer overflow by clearing the buffer.
 *
 * A newline terminator instead means the buffered bytes are not CAT traffic, since
 * no Kenwood command contains one. In a build with a USB audio interface this port
 * is also the only CDC port, so that is how a PJRC time-sync packet arrives; it is
 * handed to ApplyTimeSyncDigits(). Any other newline-terminated run of bytes is
 * dropped. Both cases exist to keep foreign bytes from being prepended to the next
 * real command, which would turn it into "?;".
 */
void CheckForCATSerialEvents(void){
    int i;
    char c;
    while( ( i = CATSerial.available() ) > 0 ){
        c = ( char )CATSerial.read();
        i--;
        catCommand[ catCommandIndex ] = c;
        #ifdef DEBUG_CAT
        Serial.print( catCommand[ catCommandIndex ] );
        #endif
        if( c == ';' ){
            // Finished reading CAT command
            #ifdef DEBUG_CAT
            Serial.println();
            #endif // DEBUG_CAT

            // Check to see if the command is a good one BEFORE sending it
            // to the command executor
            //Serial.println( String("catCommand is ")+String(catCommand)+String(" catCommandIndex is ")+String(catCommandIndex));
            char *parser_output = command_parser( catCommand );
            catCommandIndex = 0;
            // We executed it, now erase it
            memset( catCommand, 0, sizeof( catCommand ));
            if( parser_output[0] != '\0' ){
                #ifdef DEBUG_CAT1
                Serial.println( parser_output );
                #endif // DEBUG_CAT
                int i = 0;
                while( parser_output[i] != '\0' ){
                    if( CATSerial.availableForWrite() > 0 ){
                        CATSerial.print( parser_output[i] );
                        #ifdef DEBUG_CAT
                        Serial.print( parser_output[i] );
                        #endif
                        i++;
                    }else{
                        CATSerial.flush();
                    }
                }
                CATSerial.flush();
                #ifdef DEBUG_CAT
                Serial.println();
                #endif // DEBUG_CAT
            }
        }else if( c == '\n' || c == '\r' ){
            // Not CAT: no Kenwood command contains a newline. On a single-port
            // build (USB audio, where CATSerial is Serial) this is how a PJRC
            // time-sync packet arrives - 'T' + 10 digits + '\n' - because this
            // reader drains the port before CheckForSerialTimeSync() could see
            // it. Hand the digits over; anything else newline-terminated is
            // discarded rather than left to corrupt the next real command.
            if( catCommand[0] == 'T' && catCommandIndex > 0 ){
                ApplyTimeSyncDigits( &catCommand[1], (uint8_t)(catCommandIndex - 1) );
            }
            catCommandIndex = 0;
            memset( catCommand, 0, sizeof( catCommand ));
        }else{
            catCommandIndex++;
            if( catCommandIndex >= 128 ){
                catCommandIndex = 0;
                memset( catCommand, 0, sizeof( catCommand ));   //clear out that overflowed buffer!
                #ifdef DEBUG_CAT
                Serial.println( "CAT command buffer overflow" );
                #endif
            }
        }
    }
}

/**
 * Parse and execute a received CAT command
 * @param command Null-terminated command string ending with semicolon
 * @return Response string to send back to host
 *
 * Compares command against valid_commands table and calls appropriate
 * read or write handler based on command length. Returns "?;" for
 * unsupported or malformed commands.
 */
char *command_parser( char* command ){
    // loop through the entire list of supported commands
    //Debug( String("command_parser(): cmd is ") + String(command) );
    for( int i = 0; i < NUM_SUPPORTED_COMMANDS; i++ ){
        if( ! strncmp( command, valid_commands[ i ].name, 2 ) ){
            //Serial.println( String("command_parser(): found ") + String(valid_commands[i].name) );
            // The two letters match.  What about the params?
            int write_params_len = valid_commands[ i ].set_len;
            int read_params_len  = valid_commands[ i ].read_len;
      
            char* (*write_function)(char* );
            write_function = valid_commands[i].write_function;

            char* (*read_function)(char*);
            read_function = valid_commands[i].read_function;

            // A zero length means the command has no form of that kind (a
            // read-only command has set_len 0, a write-only one read_len 0).
            // Skip the test rather than indexing command[-1], which reads off
            // the front of catCommand and makes dispatch depend on whatever
            // static happens to precede it.
            if( write_params_len > 0 && command[ write_params_len - 1 ] == ';' ) return ( *write_function )( command );
            if( read_params_len  > 0 && command[ read_params_len - 1  ] == ';' ) return ( *read_function  )( command );
            // Wrong length for read OR write.  No semicolon in the right places
            sprintf( obuf, "?;");
            return obuf;
        }
    }
    Debug("Unrecognized command:"+String(command));
    // Went through the list, nothing found.
    sprintf( obuf, "?;");
    return obuf;
}  
