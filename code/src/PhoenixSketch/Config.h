#define FAST_TUNE

#define MY_LAT                                  39.07466 // Coordinates for QTH
#define MY_LON                                  -84.42677
#define MY_CALL                                 "ABCDE" // Default max is 10 chars
#define MY_TIMEZONE                             "EST: "  // Default max is 10 chars
//#define ITU_REGION                            1 //for Europe
#define ITU_REGION                              2  // for USA
//#define ITU_REGION                            3   // Asia/Oceania
#define TIME_24H // comment this out to get 12 hour time

// Default values for a fresh radio
#define CURRENT_FREQ_A 7200000L  // VFO_A
#define CURRENT_FREQ_B 7030000L  // VFO_B
#define DEFAULTFREQINCREMENT 1000 // Default: (10, 50, 100, 250, 1000, 10000Hz)
#define DEFAULT_POWER_LEVEL 5   // Startup power level
#define FAST_TUNE_INCREMENT 10  // Default from above for fine tune
#define STARTUP_BAND BAND_40M   // This is the 40M band

// Timing for startup screens
#define SPLASH_DURATION_MS 1000 // How long to show Splash screen
#define I2C_DELAY_LONG 10000L   // How long to show I2C screen with errors
#define I2C_DELAY_SHORT 1000L   // How long to show I2C screen when no error

// Control encoder direction and speed
#define VOLUME_REVERSED false
#define FILTER_REVERSED false
#define MAIN_TUNE_REVERSED false
#define FINE_TUNE_REVERSED false
#define ENCODER_FACTOR 0.25F    // use 0.25f with cheap encoders that have 4 detents per step,
                                // for other encoders or libs we use 1.0f

// CW configuration
#define CW_TRANSMIT_SPACE_TIMEOUT_MS            200 // how long to wait for another key press before exiting CW transmit state
#define DEFAULT_KEYER_WPM                       20 // Startup value for keyer wpm
#define DIT_DURATION_MS                         (60000.0f/(50.0f*DEFAULT_KEYER_WPM))
#define KEYER_TYPE                              KeyTypeId_Straight // or KeyTypeId_Keyer
#define KEYER_FLIP                              false // or true

// Set the I2C addresses of the LPF, BPF, and RF boards
#define SI5351_BUS_BASE_ADDR    0x60
#define LPF_MCP23017_ADDR       0x25
#define BPF_MCP23017_ADDR       0x24
#define RF_MCP23017_ADDR        0x27

// Front panel button functions
#define MENU_OPTION_SELECT  0
#define MAIN_MENU_UP        1
#define BAND_UP             2
#define ZOOM                3
#define RESET_TUNING        4
#define BAND_DN             5
#define TOGGLE_MODE         6
#define DEMODULATION        7
#define MAIN_TUNE_INCREMENT 8
#define NOISE_REDUCTION     9
#define NOTCH_FILTER        10
#define FINE_TUNE_INCREMENT 11
#define FILTER              12
#define DECODER_TOGGLE      13
#define DDE                 14
#define BEARING             15
#define HOME_SCREEN         17
#define VOLUME_BUTTON       18
#define FILTER_BUTTON       19
#define FINETUNE_BUTTON     20
#define VFO_TOGGLE          21