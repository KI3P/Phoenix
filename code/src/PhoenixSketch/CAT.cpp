#include "CAT.h"

// Kenwood TS2000 CAT Interface - Minimal support for WDSP-X
//
// Note that this uses SerialUSB1 for the CAT interface.
// Configure the IDE to set Tools->USB Type to Dual Serial.

// Uncomment to see CAT messages on the Serial Output
#define DEBUG_CAT

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
char *ID_read(  char* cmd );
char *IF_read(  char* cmd );
char *MD_write( char* cmd );
char *MD_read(  char* cmd );
char *MG_write( char* cmd );
char *MG_read(  char* cmd );
char *NF_write( char* cmd );
char *NF_read(  char* cmd );
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
char *SA_write( char* cmd );
char *SA_read(  char* cmd );
char *TX_write( char* cmd );
char *VX_write( char* cmd );
char *VX_read( char* cmd );
char *ED_read(  char* cmd );
char *PR_read( char* cmd);

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
#define NUM_SUPPORTED_COMMANDS 23
valid_command valid_commands[ NUM_SUPPORTED_COMMANDS ] =
    {
        { "AG", 7,  4, AG_write, AG_read },  //audio gain
        { "BD", 3,  0, BD_write, unsupported_cmd }, //band down, no read, only set
        { "BU", 3,  0, BU_write, unsupported_cmd }, //band up
        { "DB", 3+4,3, DB_write, unsupported_cmd }, //dBm calibration
        { "FA", 14, 3, FA_write, FA_read },  //VFO A
        { "FB", 14, 3, FB_write, FB_read },  //VFO B
        { "FR", 14, 3, FR_write, FR_read }, //RECEIVE VFO
        { "FT", 14, 3, FT_write, FT_read }, //XMT VFO
        { "ID", 0,  3, unsupported_cmd, ID_read }, // RADIO ID#, read-only
        { "IF", 0,  3, unsupported_cmd, IF_read }, //radio status, read-only
        { "MD", 4,  3, MD_write, MD_read }, //operating mode, CW, USB etc
        { "MG", 6,  3, MG_write, MG_read }, // mike gain
        { "NF", 3+3,  3, NF_write, NF_read }, // spectrum noise floor. 3 digit number
        { "NR", 4,  3, NR_write, NR_read }, // Noise reduction function: 0=off
        { "NT", 4,  3, NT_write, NT_read }, // Auto Notch 0=off, 1=ON
        { "PC", 6,  3, PC_write, PC_read }, // output power
        { "PD", 0,  3, unsupported_cmd, PD_read }, // read the PSD -- NOT a Kenwood keyword
        { "PS", 4,  3, PS_write, PS_read },  // Rig power on/off
        { "RX", 3,  0, RX_write, unsupported_cmd },  // Receiver function 0=main 1=sub
        { "TX", 3,  0, TX_write, unsupported_cmd }, // set transceiver to transmit.
        { "VX", 3+1, 3, VX_write, VX_read }, // VOX write/read
        { "ED", 0,  3, unsupported_cmd, ED_read }, // print out the state of the EEPROM data -- NOT a Kenwood keyword
        { "PR", 0,  3, unsupported_cmd, PR_read } // print out the state of the hardware register -- NOT a Kenwood keyword
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
    Serial.print( "AG_read()!\n");
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
    ED.currentBand[vfo] = GetBand(freq);
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
    sprintf( obuf, "FA%011lld;", ED.centerFreq_Hz[VFO_A] );
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
    sprintf( obuf, "FB%011lld;", ED.centerFreq_Hz[VFO_B] );
    return obuf;
}

/**
 * CAT command FT - Set transmit frequency (assumes no SPLIT operation)
 * @param cmd CAT command string with frequency after position 2
 * @return Response string with set frequency
 */
char *FT_write( char* cmd  ){
    //Assuming not SPLIT;  fix it later.
    long freq = atol( &cmd[ 2 ] );
    set_vfo( freq, ED.activeVFO );
    sprintf( obuf, "FT%011ld;", freq );
    return obuf;
}

/**
 * CAT command FT - Read transmit frequency (assumes no SPLIT operation)
 * @param cmd CAT command string
 * @return Response string with current transmit frequency
 */
char *FT_read(  char* cmd  ){
    //Assuming not SPLIT; fix it later.
    sprintf( obuf, "FT%011lld;", GetTXRXFreq_dHz()/100 );
    return obuf;
}

/**
 * CAT command FR - Set receive frequency (assumes no SPLIT operation)
 * @param cmd CAT command string with frequency after position 2
 * @return Response string with set frequency
 */
char *FR_write( char* cmd  ){
    long freq = atol( &cmd[ 2 ] );
    //Assuming not SPLIT; fix it later.
    set_vfo( freq, ED.activeVFO );
    sprintf( obuf, "FR%011ld;", freq);
    return obuf;
}

/**
 * CAT command FR - Read receive frequency (assumes no SPLIT operation)
 * @param cmd CAT command string
 * @return Response string with current receive frequency
 */
char *FR_read(  char* cmd  ){
    sprintf( obuf, "FT%011lld;", GetTXRXFreq_dHz()/100 );
    return obuf;
}

/**
 * CAT command ID - Read radio identification
 * @param cmd CAT command string
 * @return Response "ID019;" (Kenwood TS-2000 identifier)
 */
char *ID_read(  char* cmd  ){
    sprintf( obuf, "ID019;");
    return obuf;                            // Kenwood TS-2000 ID
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
        switch( bands[ ED.currentBand[ED.activeVFO] ].mode ){
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
    if ((modeSM.state_id == ModeSm_StateId_CW_RECEIVE) | (modeSM.state_id == ModeSm_StateId_SSB_RECEIVE)){
        rxtx = 0;
    } else {
        rxtx = 1;
    }

    sprintf( obuf,
             "IF%011lld%04ld%+06d%d%d%d%02d%d%d%d%d%d%d%02d;",
             ED.centerFreq_Hz[ED.activeVFO],
             ED.freqIncrement, // freqIncrement 
             0, // rit
             0, // rit enabled
             0, // xit enabled
             0, 0, // Channel bank
             rxtx, // RX/TX (always RX in test)
             mode, // operating mode
             0, // RX VFO
             0, // Scan Status
             0, // split,
             0, // CTCSS enabled
             0 // CTCSS
              );
    return obuf;
}

/**
 * CAT command MD - Set operating mode (LSB, USB, CW, AM)
 * @param cmd CAT command string with mode number: 1=LSB, 2=USB, 3=CW, 5=AM
 * @return Empty string
 */
char *MD_write( char* cmd  ){
    int p1 = atoi( &cmd[2] );
    switch( p1 ){
        case 1: // LSB
            bands[ ED.currentBand[ED.activeVFO] ].mode = LSB;
            SetInterrupt(iMODE);
            break;
        case 2: // USB
            bands[ ED.currentBand[ED.activeVFO] ].mode = USB;
            SetInterrupt(iMODE);
            break;

        case 3: // CW
            // Change to CW mode if in SSB receive mode, otherwise ignore:
            if (modeSM.state_id == ModeSm_StateId_SSB_RECEIVE){
                if( ED.currentBand[ED.activeVFO] < BAND_30M ){
                    bands[ ED.currentBand[ED.activeVFO] ].mode = LSB;
                }else{
                    bands[ ED.currentBand[ED.activeVFO] ].mode = USB;
                }
                ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
                SetInterrupt(iMODE);
            }
            break;
        case 5: // AM
            bands[ ED.currentBand[ED.activeVFO] ].mode = SAM; // default to SAM rather than AM
            SetInterrupt(iMODE);
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
    if( bands[ ED.currentBand[ED.activeVFO] ].mode == LSB ){ sprintf( obuf, "MD1;" ); return obuf; }
    if( bands[ ED.currentBand[ED.activeVFO] ].mode == USB ){ sprintf( obuf, "MD2;" ); return obuf; }
    if( bands[ ED.currentBand[ED.activeVFO] ].mode == AM  ){ sprintf( obuf, "MD5;" ); return obuf; }
    if( bands[ ED.currentBand[ED.activeVFO] ].mode == SAM ){ sprintf( obuf, "MD5;" ); return obuf; }
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
 * CAT command NF - Set spectrum noise floor for current band
 * @param cmd CAT command string with noise floor value
 * @return Empty string
 */
char *NF_write( char* cmd ){
    int nf = atoi( &cmd[2] );
    Debug(nf);
    ED.spectrumNoiseFloor[ED.currentBand[ED.activeVFO]] = nf;
    return empty_string_p;
}

/**
 * CAT command NF - Read spectrum noise floor for current band
 * @param cmd CAT command string
 * @return Response string with noise floor value
 */
char *NF_read(  char* cmd ){
    sprintf( obuf, "NF%03d;", ED.spectrumNoiseFloor[ED.currentBand[ED.activeVFO]] );
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
        ED.powerOutSSB[ED.activeVFO] = requested_power;
    } else {
        ED.powerOutCW[ED.activeVFO] = requested_power;
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
        o_param = round( ED.powerOutSSB[ED.activeVFO] );
    } else {
        o_param = round( ED.powerOutCW[ED.activeVFO] );
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
    Debug("Issuing PTT released");
    switch (modeSM.state_id){
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
    sprintf( obuf, ""); // was RX0
    return obuf;   // We'll support that later.
}

/**
 * CAT command TX - Switch to transmit mode
 * @param cmd CAT command string
 * @return Response "TX0;" after pressing PTT or key depending on current mode
 */
char *TX_write( char* cmd ){
    Debug("Issuing PTT pressed");
    switch (modeSM.state_id){
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
    sprintf( obuf, ""); // was TX0
    return obuf;
}

char *VX_write( char* cmd ){
    Debug("Got VX write, ignore");
    sprintf( obuf, ""); // expects no reply
    return obuf;
}

char *VX_read( char* cmd ){
    Debug("Got VX read, ignore");
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


/**
 * Poll SerialUSB1 for incoming CAT commands and process them
 *
 * Reads characters from the CAT serial port, buffers them until a semicolon
 * terminator is received, then parses and executes the command via command_parser().
 * Sends response back over SerialUSB1. Handles buffer overflow by clearing the buffer.
 */
void CheckForCATSerialEvents(void){
    int i;
    char c;
    while( ( i = SerialUSB1.available() ) > 0 ){
        c = ( char )SerialUSB1.read();
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
                    if( SerialUSB1.availableForWrite() > 0 ){
                        SerialUSB1.print( parser_output[i] );
                        #ifdef DEBUG_CAT
                        Serial.print( parser_output[i] );
                        #endif
                        i++;
                    }else{
                        SerialUSB1.flush();
                    }
                }
                SerialUSB1.flush();
                #ifdef DEBUG_CAT
                Serial.println();
                #endif // DEBUG_CAT
            }
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

            if( command[ write_params_len - 1 ] == ';' ) return ( *write_function )( command );
            if( command[ read_params_len - 1  ] == ';' ) return ( *read_function  )( command );
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
