/**
 * @file LPFBoard.cpp
 * @brief Low Pass Filter (LPF) board control and SWR measurement
 *
 * This module manages the external LPF/BPF board hardware that provides:
 * - Band-switched low-pass filters for harmonic suppression (160m-6m)
 * - Band-pass filters for receive and transmit paths
 * - Antenna selection (4 antenna ports)
 * - Transverter control (XVTR bypass/selection)
 * - 100W PA bypass/selection
 * - SWR (Standing Wave Ratio) measurement using directional coupler
 *
 * Hardware Interface:
 * -------------------
 * - MCP23017 I2C GPIO expander for digital control (16 pins)
 * - AD7991 4-channel ADC for forward/reflected power measurement
 * - All control signals routed through BANDS connector
 * - I2C bus: Wire2 (secondary I2C bus on Teensy 4.1)
 *
 * Register Layout:
 * ----------------
 * The hardware state is maintained in the global hardwareRegister variable:
 * - Bits 0-3: Band selection (4-bit BCD encoding for LPF selection)
 * - Bits 4-5: Antenna selection (0-3 for four antenna ports)
 * - Bit 6: XVTR_SEL (transverter enable)
 * - Bit 7: 100W_PA_SEL (100W PA bypass)
 * - Bit 8: TX BPF enable (transmit bandpass filter)
 * - Bit 9: RX BPF enable (receive bandpass filter)
 *
 * @see AD7991.h for ADC interface details
 * @see SDT.h for hardware register bit definitions
 */

#include "SDT.h"
#include "AD7991.h"

// LPF_Register bit map:
// Bit   Pin    Description     Receive            Transmit
// 0     GPB0   Band I2C0       b                  b
// 1     GPB1   Band I2C1       b                  b
// 2     GPB2   Band I2C2       b                  b
// 3     GPB3   Band I2C3       b                  b
// 4     GPB4   Antenna I2C0    a                  a
// 5     GPB5   Antenna I2C1    a                  a
// 6     GPB6   XVTR_SEL        0                  1
// 7     GPB7   100W_PA_SEL     0                  0
// 8     GPA0   TX BPF          0                  1
// 9     GPA1   RX BPF          1                  0
// 10-15 GPA2-GPA7   Not used   0                  0

#define LPF_REGISTER_STARTUP_STATE 0x020F // receive mode, antenna 0, filter bypass

static Adafruit_MCP23X17 mcpLPF;
static bool LPFinitialized = false;
static errno_t LPFerrno = EFAIL;
static uint8_t mcpA_old = 0x00;
static uint8_t mcpB_old = 0x00;
static AD7991 swrADC;

#define LPF_GPA_STATE (uint8_t)((hardwareRegister >> 8) & 0x00000003)   // Bits 8 & 9
#define LPF_GPB_STATE (uint8_t)(hardwareRegister & 0x000000FF)          // Lowest byte

#define SET_LPF_GPA(val) (hardwareRegister = (hardwareRegister & 0xFFFFFCFF) | (((uint32_t)val & 0x00000003) << 8));buffer_add()
#define SET_LPF_GPB(val) (hardwareRegister = (hardwareRegister & 0xFFFFFF00) | ((uint32_t)val  & 0x000000FF));buffer_add()
#define SET_LPF_BAND(val) (hardwareRegister = (hardwareRegister & 0xFFFFFFF0) | ((uint32_t)val & 0x0000000F));buffer_add()
#define SET_ANTENNA(val) (hardwareRegister = (hardwareRegister & 0xFFFFFFCF) | (((uint32_t)val & 0x00000003) << 4));buffer_add()

///////////////////////////////////////////////////////////////////////////////
// Unit Testing Helper Functions
///////////////////////////////////////////////////////////////////////////////

/**
 * Get the current LPF hardware register state (for unit testing).
 *
 * Returns the lower 10 bits of the hardwareRegister that control the LPF board.
 *
 * @return Lower 10 bits of hardware register (bits 0-9)
 */
uint16_t GetLPFRegisterState(void) {
    return (uint16_t)(hardwareRegister & 0x000003FF);
}

/**
 * Set the LPF hardware register state (for unit testing).
 *
 * Directly modifies the lower 10 bits of hardwareRegister without updating
 * physical hardware. Used in unit tests to set up known states.
 *
 * @param value The 10-bit value to set (only lower 10 bits used)
 */
void SetLPFRegisterState(uint16_t value) {
    hardwareRegister = (hardwareRegister & 0xFFFFFC00) | (value & 0x03FF);
}

/**
 * Get the cached MCP23017 Port A register value (for unit testing).
 *
 * Returns the last value written to MCP23017 GPIO Port A. Used to verify
 * register updates in unit tests without hardware access.
 *
 * @return Last written Port A value (8 bits)
 */
uint8_t GetLPFMCPAOld(void) {
    return mcpA_old;
}

/**
 * Get the cached MCP23017 Port B register value (for unit testing).
 *
 * Returns the last value written to MCP23017 GPIO Port B. Used to verify
 * register updates in unit tests without hardware access.
 *
 * @return Last written Port B value (8 bits)
 */
uint8_t GetLPFMCPBOld(void) {
    return mcpB_old;
}

/**
 * Set the cached MCP23017 Port A register value (for unit testing).
 *
 * Directly sets mcpA_old without hardware interaction. Used in unit tests
 * to simulate previous register states.
 *
 * @param value The 8-bit value to cache
 */
void SetLPFMCPAOld(uint8_t value) {
    mcpA_old = value;
}

/**
 * Set the cached MCP23017 Port B register value (for unit testing).
 *
 * Directly sets mcpB_old without hardware interaction. Used in unit tests
 * to simulate previous register states.
 *
 * @param value The 8-bit value to cache
 */
void SetLPFMCPBOld(uint8_t value) {
    mcpB_old = value;
}

///////////////////////////////////////////////////////////////////////////////
// Band and Hardware Control Functions
///////////////////////////////////////////////////////////////////////////////

/**
 * Convert a band enumeration to BCD encoding for LPF board hardware.
 *
 * The LPF board uses 4-bit BCD encoding to select the appropriate low-pass
 * filter for each amateur radio band. This function maps internal band
 * identifiers to hardware BCD values.
 *
 * Supported bands: 160m, 80m, 60m, 40m, 30m, 20m, 17m, 15m, 12m, 10m, 6m
 *
 * @param band Band enumeration value (BAND_160M, BAND_80M, etc.)
 * @return 4-bit BCD value for hardware, or BAND_NF_BCD if band not found
 */
uint8_t BandToBCD(int32_t band){
    switch (band){
        case BAND_160M:
            return BAND_160M_BCD;
        case BAND_80M:
            return BAND_80M_BCD;
        case BAND_60M:
            return BAND_60M_BCD;
        case BAND_40M:
            return BAND_40M_BCD;
        case BAND_30M:
            return BAND_30M_BCD;
        case BAND_20M:
            return BAND_20M_BCD;
        case BAND_17M:
            return BAND_17M_BCD;
        case BAND_15M:
            return BAND_15M_BCD;
        case BAND_12M:
            return BAND_12M_BCD;
        case BAND_10M:
            return BAND_10M_BCD;
        case BAND_6M:
            return BAND_6M_BCD;
        default:
            return BAND_NF_BCD;
    }
}

/**
 * Initialize the MCP23017 GPIO expander on the LPF board.
 *
 * Performs hardware initialization sequence:
 * 1. Sets up initial hardware register state for receive mode
 * 2. Attempts I2C communication with MCP23017 at LPF_MCP23017_ADDR
 * 3. Configures all 16 GPIO pins as outputs
 * 4. Writes initial state to GPIO ports A and B
 * 5. Updates BIT (Built-In Test) results for hardware presence
 *
 * This function uses lazy initialization - subsequent calls return cached
 * errno value without re-initializing hardware.
 *
 * Initial state: Receive mode, antenna 0, band filters active, no PA/XVTR
 *
 * @return ESUCCESS if initialization succeeded, ENOI2C if MCP23017 not found
 */
errno_t InitLPFBoardMCP(void){
    if (LPFinitialized) return LPFerrno;

    /******************************************************************
     * Set up the LPF which is connected via the BANDS connector *
     ******************************************************************/
    // Prepare the hardware register values for receive mode
    SET_LPF_BAND(BandToBCD(ED.currentBand[ED.activeVFO]));
    SET_ANTENNA(ED.antennaSelection[ED.currentBand[ED.activeVFO]]);
    CLEAR_BIT(hardwareRegister,PA100WBIT);
    CLEAR_BIT(hardwareRegister,RXTXBIT);
    CLEAR_BIT(hardwareRegister,XVTRBIT);
    CLEAR_BIT(hardwareRegister,TXBPFBIT);
    SET_BIT(hardwareRegister,RXBPFBIT);

    if (mcpLPF.begin_I2C(LPF_MCP23017_ADDR,&Wire2)){
        Debug("Initializing LPF board");
        mcpLPF.enableAddrPins();
        // Set all pins to be outputs
        for (int i=0;i<16;i++){
            mcpLPF.pinMode(i, OUTPUT);
        }
        mcpA_old = LPF_GPA_STATE;
        mcpB_old = LPF_GPB_STATE;
        mcpLPF.writeGPIOA(LPF_GPA_STATE); 
        mcpLPF.writeGPIOB(LPF_GPB_STATE);
        Debug("Startup LPF GPA state: "+String(LPF_GPA_STATE,BIN));
        Debug("Startup LPF GPB state: "+String(LPF_GPB_STATE,BIN));
        bit_results.V12_LPF_I2C_present = true;
        LPFerrno = ESUCCESS;
    } else {
        Debug("LPF MCP23017 not found at 0x"+String(LPF_MCP23017_ADDR,HEX));
        bit_results.V12_LPF_I2C_present = false;
        LPFerrno = ENOI2C;
    }
    LPFinitialized = true;
    return LPFerrno;
}

/**
 * Read the current state of both MCP23017 GPIO ports.
 *
 * Performs an I2C read of GPIOA and GPIOB registers from the MCP23017.
 * Returns the actual hardware state, not the cached software state.
 *
 * @return 16-bit value with Port A in lower byte, Port B in upper byte
 */
uint16_t GetLPFMCPRegisters(void){
    return mcpLPF.readGPIOAB();
}

/**
 * Update MCP23017 hardware registers if they differ from cached values.
 *
 * Implements write-on-change optimization to minimize I2C bus traffic.
 * Compares desired register states (from hardwareRegister) with cached
 * previous states (mcpA_old, mcpB_old) and only writes if changed.
 *
 * Called after any hardware register modification to push changes to physical hardware.
 */
void UpdateMCPRegisters(void){
    if (mcpA_old != LPF_GPA_STATE){
        mcpLPF.writeGPIOA(LPF_GPA_STATE); 
        mcpA_old = LPF_GPA_STATE;
    }
    if (mcpB_old != LPF_GPB_STATE){
        mcpLPF.writeGPIOB(LPF_GPB_STATE); 
        mcpB_old = LPF_GPB_STATE;
    }
}

///////////////////////////////////////////////////////////////////////////////
// Bandpass Filter Control
///////////////////////////////////////////////////////////////////////////////

/**
 * Enable the transmit bandpass filter.
 *
 * Sets the TXBPFBIT in hardwareRegister and updates the MCP23017 hardware.
 * The TX BPF provides additional filtering during transmission.
 */
void TXSelectBPF(void){
    SET_BIT(hardwareRegister, TXBPFBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

/**
 * Bypass the transmit bandpass filter.
 *
 * Clears the TXBPFBIT in hardwareRegister and updates the MCP23017 hardware.
 * Used when wider frequency coverage is needed or for troubleshooting.
 */
void TXBypassBPF(void){
    CLEAR_BIT(hardwareRegister, TXBPFBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

/**
 * Enable the receive bandpass filter.
 *
 * Sets the RXBPFBIT in hardwareRegister and updates the MCP23017 hardware.
 * The RX BPF improves selectivity and reduces out-of-band interference.
 */
void RXSelectBPF(void){
    SET_BIT(hardwareRegister, RXBPFBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

/**
 * Bypass the receive bandpass filter.
 *
 * Clears the RXBPFBIT in hardwareRegister and updates the MCP23017 hardware.
 * Used when wider frequency coverage is needed or for troubleshooting.
 */
void RXBypassBPF(void){
    CLEAR_BIT(hardwareRegister, RXBPFBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

/**
 * Initialize bandpass filter control hardware.
 *
 * Wrapper function that calls InitLPFBoardMCP() to initialize the MCP23017
 * GPIO expander used for BPF control.
 *
 * @return ESUCCESS if initialization succeeded, ENOI2C if hardware not found
 */
errno_t InitBPFPathControl(void){
    return InitLPFBoardMCP();
}

///////////////////////////////////////////////////////////////////////////////
// Transverter Control
///////////////////////////////////////////////////////////////////////////////

/**
 * Enable the transverter (XVTR) signal path.
 *
 * Clears the XVTRBIT in hardwareRegister (active low logic) and updates
 * hardware. When selected, RF is routed to/from the transverter port for
 * operation on bands like 2m, 70cm, or microwave frequencies.
 */
void SelectXVTR(void){
    // XVTR is active low
    CLEAR_BIT(hardwareRegister, XVTRBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

/**
 * Bypass the transverter and route RF to main antenna ports.
 *
 * Sets the XVTRBIT in hardwareRegister (active low logic) and updates
 * hardware. This is the normal operating mode for HF operation.
 */
void BypassXVTR(void){
    SET_BIT(hardwareRegister, XVTRBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

/**
 * Initialize transverter control hardware.
 *
 * Wrapper function that calls InitLPFBoardMCP() to initialize the MCP23017
 * GPIO expander used for XVTR control.
 *
 * @return ESUCCESS if initialization succeeded, ENOI2C if hardware not found
 */
errno_t InitXVTRControl(void){
    return InitLPFBoardMCP();
}

///////////////////////////////////////////////////////////////////////////////
// 100W Power Amplifier Control
///////////////////////////////////////////////////////////////////////////////

/**
 * Enable the 100W power amplifier in the signal path.
 *
 * Sets the PA100WBIT in hardwareRegister and updates hardware. When selected,
 * transmit signals are routed through the 100W PA for higher output power.
 */
void Select100WPA(void){
    SET_BIT(hardwareRegister, PA100WBIT);
    UpdateMCPRegisters();
}

/**
 * Bypass the 100W power amplifier.
 *
 * Clears the PA100WBIT in hardwareRegister and updates hardware. Used for
 * QRP (low power) operation or when using an external amplifier.
 */
void Bypass100WPA(void){
    CLEAR_BIT(hardwareRegister, PA100WBIT);
    UpdateMCPRegisters();
}

/**
 * Initialize 100W power amplifier control hardware.
 *
 * Wrapper function that calls InitLPFBoardMCP() to initialize the MCP23017
 * GPIO expander used for PA control.
 *
 * @return ESUCCESS if initialization succeeded, ENOI2C if hardware not found
 */
errno_t Init100WPAControl(void){
    return InitLPFBoardMCP();
}

///////////////////////////////////////////////////////////////////////////////
// Low Pass Filter Band Selection
///////////////////////////////////////////////////////////////////////////////

/**
 * Select the appropriate low-pass filter for the specified band.
 *
 * Chooses the LPF that provides harmonic suppression for the given amateur
 * radio band. Special handling for out-of-band frequencies:
 * - Below 160m: Selects 160m LPF (lowest band)
 * - Between bands: Selects LPF for next higher band (FCC compliance)
 * - Above 6m: Disables LPF (forces no filter selection)
 *
 * This ensures transmit harmonics are always filtered appropriately, even
 * when operating outside ham bands (e.g., general coverage receiver mode).
 *
 * @param band Band identifier (BAND_160M, BAND_80M, etc.) or -1 for auto-selection
 */
void SelectLPFBand(int32_t band){
    if (band == -1){
        // We are in the case where the selected frequency is outside a ham band
        // We want to maintain FCC compliance on harmonic strength. So select the 
        // LPF for the nearest band that is higher than our current frequency.
        if (ED.centerFreq_Hz[ED.activeVFO] < bands[FIRST_BAND].fBandLow_Hz){
            band = FIRST_BAND;
        } else {
            for(uint8_t i = FIRST_BAND; i <= LAST_BAND-1; i++){
                if((ED.centerFreq_Hz[ED.activeVFO] > bands[i].fBandHigh_Hz) && 
                   (ED.centerFreq_Hz[ED.activeVFO] < bands[i+1].fBandLow_Hz)){
                    band = i;
                    break;
                }
            }
        }
        if (band == -1){
            // This is the case where the frequency is higher than the highest band
            band = LAST_BAND + 10; // force it to pick no filter. You're on your own now
        }
    }
    SET_LPF_BAND(BandToBCD(band));
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

/**
 * Initialize all LPF board subsystems.
 *
 * Performs complete initialization of the LPF board including:
 * - SWR measurement ADC (AD7991)
 * - GPIO expander (MCP23017) for filter/antenna control
 *
 * @return Sum of errno values (ESUCCESS=0 if all initialized successfully)
 */
errno_t InitializeLPFBoard(void){
    errno_t val = InitSWRControl();
    val += InitLPFBoardMCP();
    return val;
}

///////////////////////////////////////////////////////////////////////////////
// Antenna Selection
///////////////////////////////////////////////////////////////////////////////

/**
 * Select one of the four antenna ports.
 *
 * Routes RF signals to/from the specified antenna connector. The LPF board
 * provides 4 antenna ports (0-3) with relay switching.
 *
 * @param antennaNum Antenna port number (0-3). Invalid values are ignored with debug message.
 */
void SelectAntenna(uint8_t antennaNum){
    if ((antennaNum >= 0) & (antennaNum <=3)){
        SET_ANTENNA(antennaNum);
    } else {
        Debug(String("V12 LPF Control: Invalid antenna selection! ") + String(antennaNum));
    }
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

/**
 * Initialize antenna selection control hardware.
 *
 * Wrapper function that calls InitLPFBoardMCP() to initialize the MCP23017
 * GPIO expander used for antenna relay control.
 *
 * @return ESUCCESS if initialization succeeded, ENOI2C if hardware not found
 */
errno_t InitAntennaControl(void){
    return InitLPFBoardMCP();
}

///////////////////////////////////////////////////////////////////////////////
// SWR (Standing Wave Ratio) Measurement
///////////////////////////////////////////////////////////////////////////////

static float32_t adcF_sRawOld;
static float32_t adcR_sRawOld;
static float32_t adcF_sRaw;
static float32_t adcR_sRaw;
static float32_t Pf_dBm;
static float32_t Pr_dBm;
static float32_t Pf_W;
static float32_t Pr_W;
static float32_t swr;

#define VREF_MV 4096     // the reference voltage on your board
#define PAD_ATTENUATION_DB 26 // attenuation of the pad
#define COUPLER_ATTENUATION_DB 20 // attenuation of the binocular toroid coupler

/**
 * Read and calculate SWR, forward power, and reflected power.
 *
 * Measurement Process:
 * 1. Read forward and reflected voltage from AD7991 ADC (channels 0 and 1)
 * 2. Apply exponential moving average filter (10% new, 90% old) for smoothing
 * 3. Convert ADC readings to millivolts
 * 4. Calculate power in dBm using calibrated slope and offset per band
 * 5. Compensate for directional coupler and pad attenuation
 * 6. Convert to watts and calculate SWR from voltage reflection coefficient
 *
 * Calibration parameters (per-band):
 * - ED.SWR_F_SlopeAdj[band]: Forward channel slope adjustment
 * - ED.SWR_F_Offset[band]: Forward channel offset
 * - ED.SWR_R_SlopeAdj[band]: Reflected channel slope adjustment
 * - ED.SWR_R_Offset[band]: Reflected channel offset
 *
 * Results are stored in module-level variables accessed via ReadSWR(),
 * ReadForwardPower(), and ReadReflectedPower().
 *
 * @note Call this function periodically during transmit to update measurements
 */
void read_SWR() {
  // Step 1. Measure the peak forward and Reverse voltages
  adcF_sRaw = (float32_t)swrADC.readADCsingle(0);
  adcR_sRaw = (float32_t)swrADC.readADCsingle(1);

  adcF_sRaw = 0.1 * adcF_sRaw + 0.9 * adcF_sRawOld;  //Running average
  adcR_sRaw = 0.1 * adcR_sRaw + 0.9 * adcR_sRawOld;
  adcF_sRawOld = adcF_sRaw;
  adcR_sRawOld = adcR_sRaw;

  //Convert ADC reading to mV
  adcF_sRaw = adcF_sRaw * VREF_MV / 4096.;
  adcR_sRaw = adcR_sRaw * VREF_MV / 4096.;

  Pf_dBm = adcF_sRaw/(25 + ED.SWR_F_SlopeAdj[ED.currentBand[ED.activeVFO]]) - 84 + ED.SWR_F_Offset[ED.currentBand[ED.activeVFO]] + PAD_ATTENUATION_DB + COUPLER_ATTENUATION_DB;
  Pr_dBm = adcR_sRaw/(25 + ED.SWR_R_SlopeAdj[ED.currentBand[ED.activeVFO]]) - 84 + ED.SWR_R_Offset[ED.currentBand[ED.activeVFO]] + PAD_ATTENUATION_DB + COUPLER_ATTENUATION_DB;

  // Convert to input voltage squared as read by ADC converted to before attenuation
  Pf_W = (float32_t)pow(10,Pf_dBm/10)/1000;
  Pr_W = (float32_t)pow(10,Pr_dBm/10)/1000;

  float32_t A = pow(Pr_W / Pf_W, 0.5);
  swr = (1.0 + A) / (1.0 - A);
}

/**
 * Get the most recently calculated SWR value.
 *
 * Returns the SWR computed by the last call to read_SWR(). Values typically
 * range from 1.0 (perfect match) to 10+ (severe mismatch).
 *
 * @return SWR (Standing Wave Ratio) as a unitless ratio
 */
float32_t ReadSWR(void){
    return swr;
}

/**
 * Get the most recently calculated forward power.
 *
 * Returns the forward power computed by the last call to read_SWR().
 * This is the power being transmitted toward the antenna.
 *
 * @return Forward power in watts
 */
float32_t ReadForwardPower(void){
    return Pf_W;
}

/**
 * Get the most recently calculated reflected power.
 *
 * Returns the reflected power computed by the last call to read_SWR().
 * This is the power being reflected back from the antenna due to impedance mismatch.
 *
 * @return Reflected power in watts
 */
float32_t ReadReflectedPower(void){
    return Pr_W;
}

/**
 * Initialize the SWR measurement hardware (AD7991 ADC).
 *
 * Attempts to initialize the AD7991 4-channel ADC used for forward and
 * reflected power measurement. Tries two possible I2C addresses:
 * 1. Primary address: AD7991_I2C_ADDR1
 * 2. Alternative address: AD7991_I2C_ADDR2
 *
 * Updates BIT (Built-In Test) results with hardware presence status and
 * the I2C address where the ADC was found (if successful).
 *
 * @return ESUCCESS if ADC initialized at either address, ENOI2C if not found
 */
errno_t InitSWRControl(void){
    bit_results.V12_LPF_AD7991_present = false;
    if (swrADC.begin(AD7991_I2C_ADDR1,&Wire2)){
        bit_results.V12_LPF_AD7991_present = true;
        bit_results.AD7991_I2C_ADDR = AD7991_I2C_ADDR1;
        return ESUCCESS;
    }
    Debug("AD7991 not found at 0x"+String(AD7991_I2C_ADDR1,HEX));

    if (swrADC.begin(AD7991_I2C_ADDR2,&Wire2)){
        bit_results.V12_LPF_AD7991_present = true;
        bit_results.AD7991_I2C_ADDR = AD7991_I2C_ADDR2;
        Debug("AD7991 found at alternative 0x"+String(AD7991_I2C_ADDR2,HEX));
        return ESUCCESS;
    }
    Debug("AD7991 not found at 0x"+String(AD7991_I2C_ADDR2,HEX));
    return ENOI2C;
}

