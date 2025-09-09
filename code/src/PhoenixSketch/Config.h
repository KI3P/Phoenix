#define FAST_TUNE

#define MY_LAT                                  39.07466 // Coordinates for QTH
#define MY_LON                                  -84.42677
#define MY_CALL                                 "ABCDE" // Default max is 10 chars
#define MY_TIMEZONE                             "EST: "  // Default max is 10 chars
//#define ITU_REGION                            1 //for Europe
#define ITU_REGION                              2  // for USA
//#define ITU_REGION                            3   // Asia/Oceania

#define CW_TRANSMIT_SPACE_TIMEOUT_MS            20
#define DIT_DURATION_MS                         7
#define SPLASH_DURATION_MS                      17

#define KEYER_TYPE                              KeyTypeId_Keyer // or KeyTypeId_Straight
#define KEYER_FLIP                              false // or true

#define BUFFER_SIZE                             128
#define N_BLOCKS                                16
#define READ_BUFFER_SIZE                        (BUFFER_SIZE * N_BLOCKS)

#define PADDLE_FLIP                             false
#define DEFAULT_KEYER_WPM                       15 // Startup value for keyer wpm

//====================== System specific ===============
#define CURRENT_FREQ_A 7200000  // VFO_A
#define CURRENT_FREQ_B 7030000  // VFO_B
#define DEFAULTFREQINCREMENT 4  // Default: (10, 50, 100, 250, 1000, 10000Hz)
#define DEFAULT_POWER_LEVEL 10  // Startup power level. Probably 20 for most people
#define FAST_TUNE_INCREMENT 10  // Default from above for fine tune
#define SPLASH_DELAY 1000L      // How long to show Splash screen. Use 4000 for testing, 1000 normally
#define I2C_DELAY_LONG 10000L   // How long to show I2C screen with errors
#define I2C_DELAY_SHORT 1000L   // How long to show I2C screen when no error
#define STARTUP_BAND BAND_40M   // This is the 40M band

#define VOLUME_REVERSED false
#define FILTER_REVERSED false
#define MAIN_TUNE_REVERSED false
#define FINE_TUNE_REVERSED false

#define ENCODER_FACTOR 0.25F  // use 0.25f with cheap encoders that have 4 detents per step,
//                                                  for other encoders or libs we use 1.0f


#define SI5351_BUS_BASE_ADDR 0x60
// Set the I2C addresses of the LPF, BPF, and RF boards
#define V12_LPF_MCP23017_ADDR 0x25
#define BPF_MCP23017_ADDR 0x24
#define RF_MCP23017_ADDR 0x27

#define MENU_OPTION_SELECT 0
#define MAIN_MENU_UP 1
#define BAND_UP 2
#define ZOOM 3
#define NOISE_FLOOR 4
#define BAND_DN 5
#define SET_MODE 6
#define DEMODULATION 7
#define MAIN_TUNE_INCREMENT 8
#define NOISE_REDUCTION 9
#define NOTCH_FILTER 10
#define FINE_TUNE_INCREMENT 11
#define FILTER 12
#define DECODER_TOGGLE 13
#define DDE 14
#define BEARING 15