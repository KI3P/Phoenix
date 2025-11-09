/**
 * @file Globals.cpp
 * @brief This file contains the definitions for globally-visible variables and functions
 * 
 * The declarations for these variables and functions can be found in SDT.h. We try
 * to keep the number of global variables to a minimum.
 */

#include "SDT.h"

struct config_t ED;
bool displayFFTUpdated;
bool psdupdated = false;
float32_t psdnew[SPECTRUM_RES];
float32_t audioYPixel[SPECTRUM_RES/4];

VolumeFunction volumeFunction = AudioVolume;

//#define BROADCAST_BAND 0
#define HAM_BAND 1
//#define MISC_BAND 2
struct band bands[NUMBER_OF_BANDS] = 
  {
  //freqVFO1 freqVFO2 band low   band hi   name    mode      Hi    Low  Gain_dB  type    gain  AGC   pixel
  //                                                       filter filter                correct     offset
    1850000, 0, 1800000, 2000000, "160M", LSB, -200, -3000, 0, HAM_BAND, -2.0, 20, 20,    // 160M
#if defined(ITU_REGION) && ITU_REGION == 1
    3700000, 0,3500000, 3800000, "80M", LSB, -200, -3000, 0, HAM_BAND, -2.0, 20, 20,
    5351500, 0,5351500, 5366600, "60M", LSB, -200, -3000, 0, HAM_BAND, -2.0, 20, 20,  // 60M
    7150000, 0,7000000, 7200000, "40M", LSB, -200, -3000, 0, HAM_BAND, -2.0, 20, 20,
#elif defined(ITU_REGION) && ITU_REGION == 2
    3700000, 0,3500000, 4000000, "80M", LSB, -200, -3000, 0, HAM_BAND, -2.0, 20, 20,
    5351500, 0,5351500, 5366600, "60M", LSB, -200, -3000, 0, HAM_BAND, -2.0, 20, 20,  // 60M
    7150000, 0,7000000, 7300000, "40M", LSB, -200, -3000, 0, HAM_BAND, -2.0, 20, 20,
#elif defined(ITU_REGION) && ITU_REGION == 3
    3700000, 0,3500000, 3900000, "80M", LSB, -200, -3000, 0, HAM_BAND, -2.0, 20, 20,
    5351500, 0,5351500, 5366600, "60M", LSB, -200, -3000, 0, HAM_BAND, -2.0, 20, 20,  // 60M
    7150000, 0,7000000, 7200000, "40M", LSB, -200, -3000, 0, HAM_BAND, -2.0, 20, 20,
#endif
    10125000, 0,10100000, 10150000, "30M", USB, 3000, 200, 0, HAM_BAND, 2.0, 20, 20,  // 30M
    14200000, 0,14000000, 14350000, "20M", USB, 3000, 200, 0, HAM_BAND, 2.0, 20, 20,
    18100000, 0,18068000, 18168000, "17M", USB, 3000, 200, 0, HAM_BAND, 2.0, 20, 20,
    21200000, 0,21000000, 21450000, "15M", USB, 3000, 200, 0, HAM_BAND, 5.0, 20, 20,
    24920000, 0,24890000, 24990000, "12M", USB, 3000, 200, 0, HAM_BAND, 6.0, 20, 20,
    28350000, 0,28000000, 29700000, "10M", USB, 3000, 200, 0, HAM_BAND, 8.5, 20, 20,
    50100000, 0,50000000, 54000000, "6M", USB, 3000, 200, 0, HAM_BAND, 8.5, 20, 20,  // 6M
    70300000, 0,70000000, 72800000, "4M", USB, 3000, 200, 0, HAM_BAND, 8.5, 20, 20,   // 4M
    //142000000, 0,144000000, 148000000, "2M", USB, 3000, 200, 0, HAM_BAND, 8.5, 20, 20,   // 2M
    //	222500000, 0,220000000, 225000000, "125CM", USB, 3000, 200, 0, HAM_BAND, 8.5, 20, 20,   // 125CM
    //435000000, 0,420000000, 450000000, "70CM",  USB, 3000, 200, 0, HAM_BAND, 8.5, 20, 20,   // 70CM
    //915000000, 0,902000000, 928000000, "33CM",  USB, 3000, 200, 0, HAM_BAND, 8.5, 20, 20,   // 33CM
    //1270000000, 0,1240000000, 1300000000, "23CM", USB, 3000, 200, 0, HAM_BAND, 8.5, 20, 20  // 23CM
};

struct BIT bit_results = {
  false,
  false,
  false,
  false,
  false,
  false,
  0
};

const struct SR_Descriptor SR[18] = {
  //   SR_n ,            rate,  text
  { SAMPLE_RATE_8K, 8000, "  8k"  },       // not OK
  { SAMPLE_RATE_11K, 11025, " 11k" },     // not OK
  { SAMPLE_RATE_16K, 16000, " 16k" },      // OK
  { SAMPLE_RATE_22K, 22050, " 22k" },      // OK
  { SAMPLE_RATE_32K, 32000, " 32k" },     // OK, one more indicator?
  { SAMPLE_RATE_44K, 44100, " 44k" },      // OK
  { SAMPLE_RATE_48K, 48000, " 48k" },     // OK
  { SAMPLE_RATE_50K, 50223, " 50k" },     // NOT OK
  { SAMPLE_RATE_88K, 88200, " 88k" },      // OK
  { SAMPLE_RATE_96K, 96000, " 96k" },     // OK
  { SAMPLE_RATE_100K, 100000, "100k"},   // NOT OK
  { SAMPLE_RATE_101K, 100466, "101k"},   // NOT OK
  { SAMPLE_RATE_176K, 176400, "176k"},   // OK
  { SAMPLE_RATE_192K, 192000, "192k"},  // OK
  { SAMPLE_RATE_234K, 234375, "234k"},  // NOT OK
  { SAMPLE_RATE_256K, 256000, "256k"},  // NOT OK
  { SAMPLE_RATE_281K, 281000, "281k"},  // NOT OK
  { SAMPLE_RATE_353K, 352800, "353k"}   // NOT OK
};

ReceiveFilterConfig RXfilters;
TransmitFilterConfig TXfilters;
AGCConfig agc;
uint8_t SampleRate = SAMPLE_RATE_192K;

const float32_t CWToneOffsetsHz[] = {400, 562.5, 656.5, 750.0, 843.75 };

ModeSm modeSM;
UISm uiSM;
uint32_t hardwareRegister;

/**
 * Simple blocking delay function.
 *
 * @param millisWait Number of milliseconds to wait
 */
void MyDelay(unsigned long millisWait) {
    unsigned long now = millis();
    while (millis() - now < millisWait)
        ;  // Twiddle thumbs until delay ends...
}

/**
 * Update the CW dit duration based on current WPM setting.
 * Calculates dit length using the standard formula: 1200ms / WPM.
 * Updates modeSM.vars.ditDuration_ms with the result.
 */
void UpdateDitLength(void){
    float32_t dit_ms = 60000.0f/(50.0f*ED.currentWPM);
    modeSM.vars.ditDuration_ms = (uint16_t)round(dit_ms);
}

//////////////////////////////////////////////////
// Related to temperate and load monitoring

int64_t elapsed_micros_idx_t = 0;
int64_t elapsed_micros_sum = 0;
float32_t elapsed_micros_mean = 0.0;
elapsedMicros usec = 0;

float32_t s_hotT_ROOM; /*!< The value of s_hotTemp minus room temperature(25¡æ).*/
uint32_t panicAlarmTemp; /*!< The panic alarm temperature.*/
uint32_t roomCount;      /*!< The value of TEMPMON_TEMPSENSE0[TEMP_VALUE] at the hot temperature.*/
uint32_t s_roomC_hotC;   /*!< The value of s_roomCount minus s_hotCount.*/
uint32_t s_hotTemp;      /*!< The value of TEMPMON_TEMPSENSE0[TEMP_VALUE] at room temperature .*/
uint32_t s_hotCount;     /*!< The value of TEMPMON_TEMPSENSE0[TEMP_VALUE] at the hot temperature.*/
#define TEMPMON_ROOMTEMP 25.0f
#define TMS0_POWER_DOWN_MASK (0x1U)
#define TMS1_MEASURE_FREQ(x) (((uint32_t)(((uint32_t)(x)) << 0U)) & 0xFFFFU)

/**
 * Initialize the Teensy 4.x on-die temperature monitor.
 * Powers on the temperature sensor, configures measurement frequency,
 * and reads calibration data from OCOTP fuses for accurate temperature calculation.
 *
 * @param freq Measurement frequency in Hz
 * @param lowAlarmTemp Low temperature alarm threshold (Celsius)
 * @param highAlarmTemp High temperature alarm threshold (Celsius)
 * @param panicAlarmTemp Panic temperature alarm threshold (Celsius)
 */
void initTempMon(uint16_t freq, uint32_t lowAlarmTemp, uint32_t highAlarmTemp, uint32_t panicAlarmTemp) {
    uint32_t calibrationData;
    uint32_t roomCount;
    //first power on the temperature sensor - no register change
    TEMPMON_TEMPSENSE0 &= ~TMS0_POWER_DOWN_MASK;
    TEMPMON_TEMPSENSE1 = TMS1_MEASURE_FREQ(freq);

    calibrationData = HW_OCOTP_ANA1;
    s_hotTemp = (uint32_t)(calibrationData & 0xFFU) >> 0x00U;
    s_hotCount = (uint32_t)(calibrationData & 0xFFF00U) >> 0X08U;
    roomCount = (uint32_t)(calibrationData & 0xFFF00000U) >> 0x14U;
    s_hotT_ROOM = s_hotTemp - TEMPMON_ROOMTEMP;
    s_roomC_hotC = roomCount - s_hotCount;
}

/*****
  Purpose: Read the Teensy's temperature. Get worried over 50C

  Parameter list:
    void
  Return value:
    float           temperature Centigrade
*****/
float32_t TGetTemp() {
    uint32_t nmeas;
    float tmeas;
    while (!(TEMPMON_TEMPSENSE0 & 0x4U)) {
    ;
    }
    /* ready to read temperature code value */
    nmeas = (TEMPMON_TEMPSENSE0 & 0xFFF00U) >> 8U;
    tmeas = s_hotTemp - (float)((nmeas - s_hotCount) * s_hotT_ROOM / s_roomC_hotC);  // Calculate temperature
    return tmeas;
}

//////////////////////////////////////////////////
// Functions related to the rolling buffer of hardware state events. Used by the unit tests.

RollingBuffer buffer = {0};

/**
 * Add the current hardware register state to the rolling buffer.
 * Records a timestamped snapshot of hardwareRegister for debugging and testing.
 * The buffer wraps around when full, overwriting the oldest entry.
 */
void buffer_add(void) {
    buffer.entries[buffer.head].timestamp = micros();
    buffer.entries[buffer.head].register_value = hardwareRegister;
    buffer.head = (buffer.head + 1) % REGISTER_BUFFER_SIZE;  // Wrap around
    if (buffer.count < REGISTER_BUFFER_SIZE) {
        buffer.count++;
    }
}

/**
 * Clear all entries from the hardware register buffer.
 * Resets all timestamps and register values to zero and resets buffer pointers.
 */
void buffer_flush(void) {
    for (size_t i = 0; i < REGISTER_BUFFER_SIZE; i++){
        buffer.entries[i].timestamp = 0;
        buffer.entries[i].register_value = 0;
    }
    buffer.count = 0;
    buffer.head = 0;
}

/**
 * Print the hardware register buffer in a human-readable table format.
 * Displays all entries with timestamp, register value in decimal, binary, and hex.
 * Entries are shown from oldest to newest.
 */
void buffer_pretty_print(void) {
    Debug("=== Hardware Register Buffer Contents ===");
    Debug("Buffer size: " + String((unsigned int)buffer.count) + "/" + String((unsigned int)REGISTER_BUFFER_SIZE));
    Debug("Head index: " + String((unsigned int)buffer.head));

    if (buffer.count == 0) {
        Debug("Buffer is empty");
        return;
    }

    Debug("Entries (oldest to newest):");
    Debug("| Index | Timestamp(μs) | Register Value | Binary                                  | Hex        |");
    Debug("|-------|---------------|----------------|-----------------------------------------|------------|");

    // Calculate starting index for oldest entry
    size_t start_idx;
    if (buffer.count < REGISTER_BUFFER_SIZE) {
        start_idx = 0;
    } else {
        start_idx = buffer.head;
    }

    // Print entries from oldest to newest
    for (size_t i = 0; i < buffer.count; i++) {
        size_t idx = (start_idx + i) % REGISTER_BUFFER_SIZE;
        BufferEntry entry = buffer.entries[idx];

        // Convert register value to binary string
        String binary = "";
        for (int bit = 31; bit >= 0; bit--) {
            binary += ((entry.register_value >> bit) & 1) ? "1" : "0";
            if (bit % 4 == 0 && bit > 0) binary += " ";  // Add space every 4 bits
        }

        String line = "| ";
        line += String((unsigned int)idx, DEC).substring(0, 5);
        while (line.length() < 7) line += " ";
        line += " | ";
        line += String((unsigned int)entry.timestamp, DEC);

        // Pad timestamp 
        while (line.length() < 23) line += " ";
        line += " | ";

        line += String((unsigned int)entry.register_value, DEC);
        // Pad register value
        while (line.length() < 40) line += " ";
        line += " | ";

        line += binary;
        // Pad binary 
        while (line.length() < 82) line += " ";
        line += " | ";

        line += "0x" + String((unsigned int)entry.register_value, HEX);
        while (line.length() < 96) line += " ";
        line += "|";

        Debug(line);
    }
    Debug("==========================================");
}

/**
 * Extract and convert a bit field from a register value to a binary string.
 *
 * @param register_value The 32-bit register value
 * @param MSB Most significant bit position of the field
 * @param LSB Least significant bit position of the field
 * @return Binary string representation of the bit field (MSB first)
 */
String regtostring(uint32_t register_value,uint8_t MSB, uint8_t LSB){
  // Convert register value to binary string
  String binary = "";
  for (int bit = MSB; bit >= LSB; bit--) {
    binary += ((register_value >> bit) & 1) ? "1" : "0";
    //if (bit % 4 == 0 && bit > 0) binary += " ";  // Add space every 4 bits
  }
  return binary;
}

/**
 * Print a single buffer entry with decoded hardware register fields.
 * Displays timestamp and individual bit fields (LPF, BPF, antenna, mode flags, etc.).
 *
 * @param entry The BufferEntry to print
 */
void pretty_print_line(BufferEntry entry){
  String line = "| ";
  line += String((unsigned int)entry.timestamp, DEC);
  // Pad timestamp
  while (line.length() < 15) line += " ";
  line += " | ";
  line += regtostring(entry.register_value,LPFBAND3BIT,LPFBAND0BIT);
  line += " ";
  line += regtostring(entry.register_value,BPFBAND3BIT,BPFBAND0BIT);
  line += " ";
  line += regtostring(entry.register_value,ANT1BIT,ANT0BIT);
  line += " ";
  line += regtostring(entry.register_value,XVTRBIT,XVTRBIT);
  line += " ";
  line += regtostring(entry.register_value,PA100WBIT,PA100WBIT);
  line += " ";
  line += regtostring(entry.register_value,TXBPFBIT,TXBPFBIT);
  line += " ";
  line += regtostring(entry.register_value,RXBPFBIT,RXBPFBIT);
  line += " ";
  line += regtostring(entry.register_value,RXTXBIT,RXTXBIT);
  line += " ";
  line += regtostring(entry.register_value,CWBIT,CWBIT);
  line += " ";
  line += regtostring(entry.register_value,MODEBIT,MODEBIT);
  line += " ";
  line += regtostring(entry.register_value,CALBIT,CALBIT);
  line += " ";
  line += regtostring(entry.register_value,CWVFOBIT,CWVFOBIT);
  line += " ";
  line += regtostring(entry.register_value,SSBVFOBIT,SSBVFOBIT);
  line += " ";
  line += regtostring(entry.register_value,TXATTMSB,TXATTLSB);
  line += " ";
  line += regtostring(entry.register_value,RXATTMSB,RXATTLSB);
  line += " |";
  Debug(line);
}

/**
 * Print only the most recent buffer entry with decoded register fields.
 * Shows a table header and the last entry added to the buffer.
 * Useful for quickly checking current hardware state.
 */
void buffer_pretty_print_last_entry(void) {
  Debug("|               |              X 1     R   M   C S               |");
  Debug("|               |           A  V 0 T R X   O C A F F             |");
  Debug("|               |           n  T 0 X X T C D L O O               |");
  Debug("| Timestamp(μs) | LPF  BPF  t  R W B B X W E L O O TXATT  RXATT  |");
  Debug("|---------------|------------------------------------------------|");
  BufferEntry entry;
  if (buffer.head > 0) entry = buffer.entries[buffer.head-1];
  else entry = buffer.entries[REGISTER_BUFFER_SIZE-1];
  pretty_print_line(entry);
}

/**
 * Print all buffer entries with decoded hardware register fields.
 * Similar to buffer_pretty_print() but displays decoded bit fields
 * (LPF, BPF, antenna, mode flags, etc.) instead of raw hex values.
 * Entries are shown from oldest to newest.
 */
void buffer_pretty_buffer_array(void) {
    Debug("=== Hardware Register Buffer Contents ===");
    Debug("Buffer size: " + String((unsigned int)buffer.count) + "/" + String((unsigned int)REGISTER_BUFFER_SIZE));
    Debug("Head index: " + String((unsigned int)buffer.head));

    if (buffer.count == 0) {
        Debug("Buffer is empty");
        return;
    }

    Debug("Entries (oldest to newest):");
    Debug("|               |              X 1     R   M   C S               |");
    Debug("|               |           A  V 0 T R X   O C V V               |");
    Debug("|               |           n  T 0 X X T C D A F F               |");
    Debug("| Timestamp(μs) | LPF  BPF  t  R W B B X W E L O O TXATT  RXATT  |");
    Debug("|---------------|------------------------------------------------|");

    // Calculate starting index for oldest entry
    size_t start_idx;
    if (buffer.count < REGISTER_BUFFER_SIZE) {
        start_idx = 0;
    } else {
        start_idx = buffer.head;
    }

    // Print entries from oldest to newest
    for (size_t i = 0; i < buffer.count; i++) {
        size_t idx = (start_idx + i) % REGISTER_BUFFER_SIZE;
        BufferEntry entry = buffer.entries[idx];
        pretty_print_line(entry);
    }
    Debug("==========================================");
}

/**
 * Set debug flag pins for oscilloscope/logic analyzer monitoring.
 * Outputs a 4-bit value on GPIO pins 28-31 for timing analysis.
 *
 * @param val 4-bit value to output on pins (0-15)
 */
void Flag(uint8_t val){
    digitalWrite(31, (val >> 0) & 0b1);  //testcode
    digitalWrite(30, (val >> 1) & 0b1);  //testcode
    digitalWrite(29, (val >> 2) & 0b1);  //testcode
    digitalWrite(28, (val >> 3) & 0b1);  //testcode
}
