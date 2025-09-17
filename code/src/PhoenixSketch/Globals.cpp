#include "SDT.h"

struct config_t ED;
bool displayFFTUpdated;
bool CWLocked;
float32_t psdnew[SPECTRUM_RES];
float32_t psdold[SPECTRUM_RES];

// Used by the CW decoder
char morseCharacter;
bool morseCharacterUpdated = false;

//#define BROADCAST_BAND 0
#define HAM_BAND 1
//#define MISC_BAND 2
struct band bands[NUMBER_OF_BANDS] = 
  {
  //freqVFO1 freqVFO2 band low   band hi   name    mode      Low    Hi  Gain_dB  type    gain  AGC   pixel
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

FilterConfig filters;
AGCConfig agc;
uint8_t SampleRate = SAMPLE_RATE_192K;

const float32_t CWToneOffsetsHz[] = {400, 562.5, 656.5, 750.0, 843.75 };

float32_t SAM_carrier_freq_offset = 0;
float32_t SAM_carrier_freq_offsetOld = 0;
ModeSm modeSM;
UISm uiSM;
uint32_t hardwareRegister;

void MyDelay(unsigned long millisWait) {
    unsigned long now = millis();
    while (millis() - now < millisWait)
        ;  // Twiddle thumbs until delay ends...
}
