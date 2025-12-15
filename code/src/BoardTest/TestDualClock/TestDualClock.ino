/*
 * Use Teensy 4.1 for this test
 */

#include <Wire.h>
#include <math.h>
#include "RFBoard_si5351.h"


uint32_t hardwareRegister;

struct config_t {
    int32_t freqCorrectionFactor = 0; /** Correction value for Si5351 VFO */
    uint8_t activeVFO = 0;          /** Which VFO is currently active (0 or 1) */
    int32_t currentBand[2] = {0, 0}; /** Band for each VFO */
    int64_t centerFreq_Hz[2] = {14000000,14000000}; /** VFO center frequency for each VFO */
    int64_t fineTuneFreq_Hz[2] = {0, 0}; /** Fine tune frequency for each VFO */
} ED;

// Define a structure to hold the results of built-in-test routine
struct BIT {
    bool RF_I2C_present = false;
    bool RF_Si5351_present = false;
    bool BPF_I2C_present = false;
    bool V12_LPF_I2C_present = false;
    bool V12_LPF_AD7991_present = false;
    bool FRONT_PANEL_I2C_present = false;
    uint8_t AD7991_I2C_ADDR = 0;
} bit_results;

void MyDelay(unsigned long millisWait) {
    unsigned long now = millis();
    while (millis() - now < millisWait)
        ;  // Twiddle thumbs until delay ends...
}

// VFO related
Si5351 si5351;
#define SI5351_DRIVE_CURRENT SI5351_DRIVE_2MA
#define SI5351_LOAD_CAPACITANCE SI5351_CRYSTAL_LOAD_8PF
#define Si_5351_crystal 25000000L
static int32_t txmultiple, oldtxMultiple;
static int32_t rxmultiple, oldrxMultiple;
static int64_t SSBTXVFOFreq_dHz;
static int64_t SSBRXVFOFreq_dHz;
static int64_t CWTXVFOFreq_dHz;

void scanner(TwoWire *I2C) {
  byte error, address;
  int nDevices;

  Serial.println("Scanning...");

  nDevices = 0;
  for(address = 1; address < 127; address++ )
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    I2C->beginTransmission(address);
    error = I2C->endTransmission();

    if (error == 0)
    {
      Serial.print("I2C device found at address 0x");
      if (address<16)
        Serial.print("0");
      Serial.print(address,HEX);
      Serial.println("  !");

      nDevices++;
    }
    else if (error==4)
    {
      Serial.print("Unknown error at address 0x");
      if (address<16)
        Serial.print("0");
      Serial.println(address,HEX);
    }
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");

}

/**
 * Calculate the even divisor used in the configuration of the PLL for the SSB
 * VFO frequency.
 *
 * @param freq2_Hz The desired VFO frequency in units of Hz.
 */
int32_t EvenDivisor(int64_t freq2_Hz) {
    int32_t mult = 1;
    // Use phase method of time delay described by
    // https://tj-lab.org/2020/08/27/si5351%E5%8D%98%E4%BD%93%E3%81%A73mhz%E4%BB%A5%E4%B8%8B%E3%81%AE%E7%9B%B4%E4%BA%A4%E4%BF%A1%E5%8F%B7%E3%82%92%E5%87%BA%E5%8A%9B%E3%81%99%E3%82%8B/
    // for below 3.2MHz the ~limit of PLLA @ 400MHz for a 126 divider
    if (freq2_Hz < 100000)
        mult = 8192;

    if ((freq2_Hz >= 100000) && (freq2_Hz < 200000))   // PLLA 409.6 MHz to 819.2 MHz
        mult = 4096;

    if ((freq2_Hz >= 200000) && (freq2_Hz < 400000))   //   ""          ""
        mult = 2048;

    if ((freq2_Hz >= 400000) && (freq2_Hz < 800000))   //    ""          ""
        mult = 1024;

    if ((freq2_Hz >= 800000) && (freq2_Hz < 1600000))   //    ""         ""
        mult = 512;

    if ((freq2_Hz >= 1600000) && (freq2_Hz < 3200000))   //    ""        ""
        mult = 256;

    // Above 3.2 MHz
    if ((freq2_Hz >= 3200000) && (freq2_Hz < 6850000))   // 403.2 MHz - 863.1 MHz
        mult = 126;

    if ((freq2_Hz >= 6850000) && (freq2_Hz < 9500000))
        mult = 88;

    if ((freq2_Hz >= 9500000) && (freq2_Hz < 13600000))
        mult = 64;

    if ((freq2_Hz >= 13600000) && (freq2_Hz < 17500000))
        mult = 44;

    if ((freq2_Hz >= 17500000) && (freq2_Hz < 25000000))
        mult = 34;

    if ((freq2_Hz >= 25000000) && (freq2_Hz < 36000000))
        mult = 24;

    if ((freq2_Hz >= 36000000) && (freq2_Hz < 45000000))
        mult = 18;

    if ((freq2_Hz >= 45000000) && (freq2_Hz < 60000000))
        mult = 14;

    if ((freq2_Hz >= 60000000) && (freq2_Hz < 80000000))
        mult = 10;

    if ((freq2_Hz >= 80000000) && (freq2_Hz < 100000000))
        mult = 8;

    if ((freq2_Hz >= 100000000) && (freq2_Hz < 150000000))
        mult = 6;

    if ((freq2_Hz >= 150000000) && (freq2_Hz < 220000000))
        mult = 4;

    if(freq2_Hz>=220000000)
        mult = 2;

    return mult;
}

/**
 * Set the CLK0 and CLK1 outputs as quadrature outputs at the specified frequency.
 *
 * @param frequency_dHz The desired clock frequency in (Hz * 100)
 */
void SetSSBRXVFOFrequency(int64_t frequency_dHz){
    // No need to change if it's already at this setting
    if (frequency_dHz == SSBRXVFOFreq_dHz) return;
    SSBRXVFOFreq_dHz = frequency_dHz;
    int64_t Clk1SetFreq = frequency_dHz;
    rxmultiple = EvenDivisor(Clk1SetFreq / SI5351_FREQ_MULT);
    uint64_t pll_freq = Clk1SetFreq * rxmultiple;
    uint64_t freq = pll_freq / rxmultiple;

    if ( rxmultiple == oldrxMultiple) {               // Still within the same multiple range
        si5351.set_pll(pll_freq, SI5351_PLLA);    // just change PLLA on each frequency change of encoder
                                                  // this minimizes I2C data for each frequency change within a
                                                  // multiple range
    } else {
        if ( rxmultiple <= 126) {                                 // this the library setting of phase for freqs
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK0);  // greater than 3.2MHz where multiple is <= 126
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK1);   // set both clocks to new frequency
            si5351.set_phase(SI5351_CLK0, 0);                      // CLK0 phase = 0
            si5351.set_phase(SI5351_CLK1, rxmultiple);               // Clk1 phase = multiple for 90 degrees(digital delay)
            si5351.pll_reset(SI5351_PLLA);                         // reset PLLA to align outputs
            si5351.output_enable(SI5351_CLK0, 1);                  // set outputs on or off
            si5351.output_enable(SI5351_CLK1, 1);
            SET_BIT(hardwareRegister,SSBVFOBIT);
        }
        else {        // this is the timed delay technique for frequencies below 3.2MHz as detailed in
                    // https://tj-lab.org/2020/08/27/si5351単体で3mhz以下の直交信号を出力する/
            cli();                //__disable_irq(); or __enable_irq();     // or cli()/sei() pair; needed to get accurate timing??
            //si5351.output_enable(SI5351_CLK0, 0);  // optional switch off clocks if audio effects are generated
            //si5351.output_enable(SI5351_CLK1, 0);  //  with the change of multiple below 3.2MHz
            si5351.set_freq_manual((freq - 400ULL), pll_freq, SI5351_CLK0);  // set up frequencies of CLK 0/1 4 Hz low
            si5351.set_freq_manual((freq - 400ULL), pll_freq, SI5351_CLK1);  // as per TJ-Labs article
            si5351.set_phase(SI5351_CLK0, 0);                          // set phase registers to 0 just to be sure
            si5351.set_phase(SI5351_CLK1, 0);
            si5351.pll_reset(SI5351_PLLA);                             // align both clockss in phase
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK0);       // set clock 0  to required freq
            //delayNanoseconds(625000000);       // 62.5 * 1000000      //configured for a 62.5 mSec delay at 4 Hz difference
            delayMicroseconds(58500);                       //nominally 62500 this figure can be adjusted for a more exact delay which is phase
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK1);       // set CLK 1 to the required freq after delay
            sei();
            si5351.output_enable(SI5351_CLK0, 1);                      // switch them on to be sure
            si5351.output_enable(SI5351_CLK1, 1);                      //    ""        ""
            SET_BIT(hardwareRegister,SSBVFOBIT);
        }
    }
    oldrxMultiple = rxmultiple;
}

/**
 * Set the CLK4 and CLK5 outputs as quadrature outputs at the specified frequency.
 *
 * @param frequency_dHz The desired clock frequency in (Hz * 100)
 */
void SetSSBTXVFOFrequency(int64_t frequency_dHz){
    // No need to change if it's already at this setting
    if (frequency_dHz == SSBTXVFOFreq_dHz) return;
    SSBTXVFOFreq_dHz = frequency_dHz;
    int64_t Clk5SetFreq = frequency_dHz;
    txmultiple = EvenDivisor(Clk5SetFreq / SI5351_FREQ_MULT);
    uint64_t pll_freq = Clk5SetFreq * txmultiple;
    uint64_t freq = pll_freq / txmultiple;

    if ( txmultiple == oldtxMultiple) {               // Still within the same multiple range
        si5351.set_pll(pll_freq, SI5351_PLLB);    // just change PLLB on each frequency change of encoder
                                                  // this minimizes I2C data for each frequency change within a
                                                  // multiple range
    } else {
        if ( txmultiple <= 126) {                                 // this the library setting of phase for freqs
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK4);  // greater than 3.2MHz where multiple is <= 126
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK5);   // set both clocks to new frequency
            si5351.set_phase(SI5351_CLK4, 0);                      // CLK4 phase = 0
            si5351.set_phase(SI5351_CLK5, txmultiple);               // Clk5 phase = multiple for 90 degrees(digital delay)
            si5351.pll_reset(SI5351_PLLB);                         // reset PLLB to align outputs
            si5351.output_enable(SI5351_CLK4, 1);                  // set outputs on or off
            si5351.output_enable(SI5351_CLK5, 1);
            SET_BIT(hardwareRegister,SSBVFOBIT);
        }
        else {        // this is the timed delay technique for frequencies below 3.2MHz as detailed in
                    // https://tj-lab.org/2020/08/27/si5351単体で3mhz以下の直交信号を出力する/
            cli();                //__disable_irq(); or __enable_irq();     // or cli()/sei() pair; needed to get accurate timing??
            //si5351.output_enable(SI5351_CLK4, 0);  // optional switch off clocks if audio effects are generated
            //si5351.output_enable(SI5351_CLK5, 0);  //  with the change of multiple below 3.2MHz
            si5351.set_freq_manual((freq - 400ULL), pll_freq, SI5351_CLK4);  // set up frequencies of CLK 4/5 4 Hz low
            si5351.set_freq_manual((freq - 400ULL), pll_freq, SI5351_CLK5);  // as per TJ-Labs article
            si5351.set_phase(SI5351_CLK4, 0);                          // set phase registers to 0 just to be sure
            si5351.set_phase(SI5351_CLK5, 0);
            si5351.pll_reset(SI5351_PLLB);                             // align both clocks in phase
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK4);       // set clock 4 to required freq
            //delayNanoseconds(625000000);       // 62.5 * 1000000      //configured for a 62.5 mSec delay at 4 Hz difference
            delayMicroseconds(58500);                       //nominally 62500 this figure can be adjusted for a more exact delay which is phase
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK5);       // set CLK 5 to the required freq after delay
            sei();
            si5351.output_enable(SI5351_CLK4, 1);                      // switch them on to be sure
            si5351.output_enable(SI5351_CLK5, 1);                      //    ""        ""
            SET_BIT(hardwareRegister,SSBVFOBIT);
        }
    }
    oldtxMultiple = txmultiple;
}

// CW VFO Control Functions

/**
 * Set the CW VFO frequency for Morse code transmission.
 *
 * Configures the Si5351 CLK6 output frequency used for CW (Morse code) operation.
 * The frequency is stored and set in deci-Hertz units (Hz × 10) for high precision.
 *
 * Optimization: Skips the Si5351 write if the requested frequency matches the
 * current setting, preventing unnecessary I2C transactions.
 *
 * In CW mode, this frequency typically includes an offset for the sidetone pitch
 * (e.g., centerFreq + 700Hz for a 700Hz tone). The tune state machine handles
 * this offset calculation.
 *
 * @param frequency_dHz Desired CW VFO frequency in deci-Hertz (Hz × 10)
 *
 * @see GetCWVFOFrequency() to read current frequency
 * @see EnableCWVFOOutput() to enable CLK6 output
 * @see Tune.cpp for CW frequency offset handling
 */
void SetCWTXVFOFrequency(int64_t frequency_dHz){
    // No need to change if it's already at this setting
    if (frequency_dHz == CWTXVFOFreq_dHz) return;
    CWTXVFOFreq_dHz = frequency_dHz;
    si5351.set_freq(CWTXVFOFreq_dHz, SI5351_CLK6);
}

/**
 * Set up the communication with the Si5351, initialize its capacitance and crystal
 * settings, and initialize the clock signals
 */
errno_t InitVFOs(void){
    si5351.reset();
    MyDelay(100L);
    if (!si5351.init(SI5351_LOAD_CAPACITANCE, Si_5351_crystal, ED.freqCorrectionFactor)) {
        bit_results.RF_Si5351_present = false;
        Debug("Initialize si5351 failed!");
        return EFAIL;
    } else {
        bit_results.RF_Si5351_present = true;
    }
    MyDelay(100L);
    Serial.println("Set drive strengths");
    // Set driveCurrentSSB_mA to appropriate value
    si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_CURRENT);
    si5351.drive_strength(SI5351_CLK1, SI5351_DRIVE_CURRENT);
    si5351.drive_strength(SI5351_CLK2, SI5351_DRIVE_CURRENT);
    si5351.drive_strength(SI5351_CLK3, SI5351_DRIVE_CURRENT);
    si5351.drive_strength(SI5351_CLK4, SI5351_DRIVE_CURRENT);
    si5351.drive_strength(SI5351_CLK5, SI5351_DRIVE_CURRENT);
    si5351.drive_strength(SI5351_CLK6, SI5351_DRIVE_CURRENT);
    si5351.drive_strength(SI5351_CLK7, SI5351_DRIVE_CURRENT);

    Serial.println("Set ms sources");
    si5351.set_ms_source(SI5351_CLK0, SI5351_PLLA);
    si5351.set_ms_source(SI5351_CLK1, SI5351_PLLA);
    si5351.set_ms_source(SI5351_CLK2, SI5351_PLLA);
    si5351.set_ms_source(SI5351_CLK3, SI5351_PLLA);
    si5351.set_ms_source(SI5351_CLK4, SI5351_PLLB);
    si5351.set_ms_source(SI5351_CLK5, SI5351_PLLB);
    si5351.set_ms_source(SI5351_CLK6, SI5351_PLLB);
    si5351.set_ms_source(SI5351_CLK7, SI5351_PLLB);

    Serial.println("Enable clock outputs");
    si5351.output_enable(SI5351_CLK0, 1);
    si5351.output_enable(SI5351_CLK1, 1);
    si5351.output_enable(SI5351_CLK2, 0);
    si5351.output_enable(SI5351_CLK3, 0);
    si5351.output_enable(SI5351_CLK4, 1);
    si5351.output_enable(SI5351_CLK5, 1);
    si5351.output_enable(SI5351_CLK6, 1);
    si5351.output_enable(SI5351_CLK7, 0);

    Serial.println("Set RX freq");
    SetSSBRXVFOFrequency(10000000L*100);
    Serial.println("Set TX freq");
    SetSSBTXVFOFrequency( 5000000L*100);
    Serial.println("Set CW freq");
    SetCWTXVFOFrequency(  5000001L*100);

    return ESUCCESS;
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(500);
  Serial.println("Dual VFO test sketch");
  Wire.begin();
  
  //scanner(&Wire);

  /* Initialise the LO */
  Serial.println("Initialize VFOs...");
  InitVFOs();
  Serial.println("Setup complete!");
}

void loop() {
  // print the state and selection menu
  Serial.println("------------------------------");
  Serial.println("Select option and hit enter:");
  Serial.println("TX - SSB TX Frequency (CLK 4/5)");
  Serial.println("RX - SSB RX Frequency (CLK 0/1)");
  Serial.println("CW - CW TX Frequency  (CLK 6)");
  Serial.println("------------------------------");

  Serial.setTimeout(2000);  // 2 second timeout for slower typing
  while (Serial.available() == 0) {}     //wait for data available
  String selection = Serial.readString();  //read until timeout
  selection.trim(); // remove any \r \n whitespace at the end of the String
  selection.toUpperCase(); // accept lowercase input
  if (selection == "TX"){
      Serial.println("Enter the TX frequency in kHz");
      while (Serial.available() == 0) {}     //wait for data available
      String fsel = Serial.readString();  //read until timeout
      fsel.trim(); // remove any \r \n whitespace at the end of the String
      // Convert to int
      long f_kHz = fsel.toInt();
      SetSSBTXVFOFrequency((f_kHz*1000L)*100L);
  }
  if (selection == "RX"){
      Serial.println("Enter the RX frequency in kHz");
      while (Serial.available() == 0) {}     //wait for data available
      String fsel = Serial.readString();  //read until timeout
      fsel.trim(); // remove any \r \n whitespace at the end of the String
      // Convert to int
      long f_kHz = fsel.toInt();
      SetSSBRXVFOFrequency((f_kHz*1000L)*100L);
  }
  if (selection == "CW"){
      Serial.println("Enter the CW frequency in kHz");
      while (Serial.available() == 0) {}     //wait for data available
      String fsel = Serial.readString();  //read until timeout
      fsel.trim(); // remove any \r \n whitespace at the end of the String
      // Convert to int
      long f_kHz = fsel.toInt();
      SetCWTXVFOFrequency((f_kHz*1000L)*100L);
  } else if (selection != "TX" && selection != "RX") {
      Serial.print("Invalid selection: '");
      Serial.print(selection);
      Serial.println("'. Please enter TX, RX, or CW.");
  }
  Serial.println("------------------------------");

}
