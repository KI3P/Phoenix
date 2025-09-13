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
char *NR_write( char* cmd );
char *NR_read(  char* cmd );
char *NT_write( char* cmd );
char *NT_read(  char* cmd );
char *PC_write( char* cmd );
char *PC_read(  char* cmd );
char *PS_write( char* cmd );
char *PS_read(  char* cmd );
char *RX_write( char* cmd );
char *SA_write( char* cmd );
char *SA_read(  char* cmd );
char *TX_write( char* cmd );

typedef struct	{
	char name[3];   //two chars plus zero terminator
	int set_len;
	int read_len;
  	char* (*write_function)( char* );  //pointer to write function. Takes a pointer to the command packet, and its length. Returns result as char*
	char* (*read_function)(  char* );  //pointer to read function. Takes a pointer to the command packet, and its length. Returns?
} valid_command;

// The command_parser will compare the CAT command received against the entires in
// this array. If it matches, then it will call the corresponding write_function
// or the read_function, depending on the length of the command string.
#define NUM_SUPPORTED_COMMANDS 13
valid_command valid_commands[ NUM_SUPPORTED_COMMANDS ] =
	{
		{ "AG", 7,  4, AG_write, AG_read },  //audio gain
		{ "BD", 3,  0, BD_write, unsupported_cmd }, //band down, no read, only set
		{ "BU", 3,  0, BU_write, unsupported_cmd }, //band up
		{ "FA", 14, 3, FA_write, FA_read },  //VFO A
		{ "FB", 14, 3, FB_write, FB_read },  //VFO B
		{ "FR", 14, 3, FR_write, FR_read }, //RECEIVE VFO
		{ "FT", 14, 3, FT_write, FT_read }, //XMT VFO
		{ "ID", 0,  3, unsupported_cmd, ID_read }, // RADIO ID#, read-only
		{ "IF", 0,  3, unsupported_cmd, IF_read }, //radio status, read-only
		{ "MD", 4,  3, MD_write, MD_read }, //operating mode, CW, USB etc
		{ "MG", 6,  3, MG_write, MG_read }, // mike gain
		{ "NR", 4,  3, NR_write, NR_read }, // Noise reduction function: 0=off
		{ "NT", 4,  3, NT_write, NT_read }, // Auto Notch 0=off, 1=ON
		/*{ "PC", 6,  3, PC_write, PC_read }, // output power

		{ "PS", 4,  3, PS_write, PS_read },  // Rig power on/off
		{ "RX", 3,  0, RX_write, unsupported_cmd },  // Receiver function 0=main 1=sub
		{ "SA", 10, 3, SA_write, SA_read },  //Satellite mode
		{ "TX", 4, 0, TX_write, unsupported_cmd } // set transceiver to transmit.
*/
	};

char *unsupported_cmd( char *cmd ){
 	sprintf( obuf, "?;");
  	return obuf;
}

/**
 * Return the audio volume contained in the EEPROMData->audioVolume variable
 */
char *AG_read(  char* cmd ){
  	Serial.print( "AG_read()!\n");
  	sprintf( obuf, "AG%c%03d;", cmd[ 2 ], ( int32_t )( ( ( float32_t )ED.audioVolume * 255.0 ) / 100.0 ) );
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
	SetInterrupt(iUPDATE_TUNE);
}

void set_vfo_a( long freq ){
	set_vfo(freq, VFO_A);
}

void set_vfo_b( long freq ){
	set_vfo(freq, VFO_B);
}

char *FA_write( char* cmd ){
  	long freq = atol( &cmd[ 2 ] );
  	set_vfo_a( freq );
	sprintf( obuf, "FA%011ld;", freq );
  	return obuf;
}

char *FA_read(  char* cmd  ){
	sprintf( obuf, "FA%011ld;", ED.centerFreq_Hz[VFO_A] ); 
  	return obuf;
}

//VFO B frequency
char *FB_write( char* cmd  ){
  	long freq = atol( &cmd[ 2 ] );
  	set_vfo_b( freq );
	sprintf( obuf, "FB%011ld;", freq );
  	return obuf;	
}

//VFO B
char *FB_read(  char* cmd  ){  
  	sprintf( obuf, "FB%011ld;", ED.centerFreq_Hz[VFO_B] );
  	return obuf;
}

// Transmit Frequency
char *FT_write( char* cmd  ){
  	//Assuming not SPLIT;  fix it later.
  	long freq = atol( &cmd[ 2 ] );
	set_vfo( freq, ED.activeVFO );
	sprintf( obuf, "FT%011ld;", freq );
  	return obuf;
}

// Transmit Frequency
char *FT_read(  char* cmd  ){
  	//Assuming not SPLIT; fix it later.
	sprintf( obuf, "FT%011ld;", GetTXRXFreq_dHz()/100 );
	return obuf;
}

// Receive Frequency
char *FR_write( char* cmd  ){
  	long freq = atol( &cmd[ 2 ] );
  	//Assuming not SPLIT; fix it later.
	set_vfo( freq, ED.activeVFO );
	sprintf( obuf, "FR%011ld;", freq);
  	return obuf;
}

// Receive Frequency
char *FR_read(  char* cmd  ){
	sprintf( obuf, "FT%011ld;", GetTXRXFreq_dHz()/100 );
  	return obuf;
}

char *ID_read(  char* cmd  ){
  	sprintf( obuf, "ID019;");
  	return obuf;                            // Kenwood TS-2000 ID
}

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
	         "IF%011ld%04d%+06d%d%d%d%02d%d%d%d%d%d%d%02d%d;",
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
	         0, // CTCSS
	         0 );
  	return obuf;
}

char *MD_write( char* cmd  ){
  	int p1 = atoi( &cmd[2] );
  	bool xmtMode_changed = false;
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

char *MG_read(  char* cmd ){
	// convert from -40 .. 30 to 0..100
	int g = ( int )( ( double )( ED.currentMicGain + 40 ) * 100.0 / 70.0 );
	sprintf( obuf, "MG%03d;", g );
  	return obuf;
}


char *NR_write( char* cmd ){
  	if( cmd[ 2 ] == '0' ){
		ED.nrOptionSelect = NROff;
	}else{
		ED.nrOptionSelect = (NoiseReductionType) atoi( &cmd[2] );
	}
  	return empty_string_p;
}

//Noise reduction
char *NR_read(  char* cmd ){
  	sprintf( obuf, "NR%d;", ED.nrOptionSelect );
  	return obuf;
}


//Auto-notch
char *NT_write( char* cmd ){
  	return empty_string_p;
}

//auto-notch
char *NT_read(  char* cmd ){
  	return empty_string_p;
}

/*
//output power - for now, just spit back what they gave us.
char *PC_write( char* cmd ){
  	int requested_power = atoi( &cmd[ 3 ]);
  	//SetRF_OutputPower( (float)requested_power );  now SetRF_OutAtten()... figure out  the dB's later. JLK
  	sprintf( obuf, "PC%03d;", requested_power );
  	return obuf;
}

//output power
char *PC_read(  char* cmd ){
  	unsigned int o_param;
  	o_param = round( transmitPowerLevel );
  	sprintf( obuf, "PC%03d;", o_param );
  	return obuf;
}

// Turn the power off
char *PS_write( char* cmd ){
 	// Ask the AtTiny to do it?
  	sprintf( obuf, "?;");
  	return obuf;    //Nope.  Not doing that.
}

// Tell them that the power's on.
char *PS_read(  char* cmd ){
  	sprintf( obuf, "PS0;");
  	return obuf;          // The power's on.  Otherwise, we're not answering!
}

//Choose main or subreceiver
char *RX_write( char* cmd ){
  	catTX = false;
	sprintf( obuf, "RX0;");
  	return obuf;   // We'll support that later.
}

char *SA_write( char* cmd ){
  	sprintf( obuf, "?;");
  	return obuf;//Nope, we ain't doing that.  Yet.  
}

//Satellite Mode - not supporting for now
char *SA_read(  char* cmd ){
  	sprintf( obuf, "SA000000000000000;" );
  	return obuf;
}

//Transmit NOW!
char *TX_write( char* cmd ){
  	catTX = true;
  	sprintf( obuf, "TX0;");
  	return obuf;
}

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
      		Serial.println( String("catCommand is ")+String(catCommand)+String(" catCommandIndex is ")+String(catCommandIndex));
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

char *command_parser( char* command ){
	// loop through the entire list of supported commands
	Serial.println( String("command_parser(): cmd is ") + String(command) );
	for( int i = 0; i < NUM_SUPPORTED_COMMANDS; i++ ){
		if( ! strncmp( command, valid_commands[ i ].name, 2 ) ){
			Serial.println( String("command_parser(): found ") + String(valid_commands[i].name) );
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
	// Went through the list, nothing found.
  	sprintf( obuf, "?;");
	return obuf;
}  
