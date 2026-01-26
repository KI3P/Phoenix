#define MY_CALL                                 "ABCDE" // Default max is 10 chars
#define MY_TIMEZONE                             "EST: "  // Default max is 10 chars
//#define ITU_REGION                            1  // Europe
#define ITU_REGION                              2  // USA
//#define ITU_REGION                            3  // Asia/Oceania
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

// Control encoder direction and speed
#define FAST_TUNE // comment this out to disable the FAST_TUNE algorithm
#define VOLUME_REVERSED false
#define FILTER_REVERSED false 
#define MAIN_TUNE_REVERSED false
#define FINE_TUNE_REVERSED true 
#define ENCODER_FACTOR 0.25F    // use 0.25f with cheap encoders that have 4 detents per step,
                                // for other encoders or libs we use 1.0f

// Uncomment to use the Direct coupled transmit
//#define DIRECT_COUPLED_TX

// Uncomment to use the Analog SWR instead of the Digital SWR on the display
//#define USE_ANALOG_SWR   // uncomment to use analog SWR on pins 26/27

// CW configuration
#define CW_TRANSMIT_SPACE_TIMEOUT_MS            200 // how long to wait for another key press before exiting CW transmit state
#define DEFAULT_KEYER_WPM                       20 // Startup value for keyer wpm
#define DIT_DURATION_MS                         (60000.0f/(50.0f*DEFAULT_KEYER_WPM))
#define KEYER_TYPE                              KeyTypeId_Straight // or KeyTypeId_Keyer
#define KEYER_FLIP                              false // or true

// Set the I2C addresses of the LPF, BPF, RF, and front panel boards
#define SI5351_BUS_BASE_ADDR    0x60 
#define SI5351_DUAL_VFO_ADDR    0x61     // The I2C address with split VFO hardware
#define LPF_MCP23017_ADDR       0x25
#define BPF_MCP23017_ADDR       0x24
#define RF_MCP23017_ADDR        0x27
#define V12_PANEL_MCP23017_ADDR_1 0x20
#define V12_PANEL_MCP23017_ADDR_2 0x21
// LPF board AD7991 chip comes with two possible addresses depending on part number
// This is not a parameter you need to change
#define AD7991_I2C_ADDR1 0x28
#define AD7991_I2C_ADDR2 0x29

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
#define DFE                 14
#define BEARING             15
#define SPARE               16
#define HOME_SCREEN         17
#define VOLUME_BUTTON       18
#define FILTER_BUTTON       19
#define FINETUNE_BUTTON     20
#define VFO_TOGGLE          21