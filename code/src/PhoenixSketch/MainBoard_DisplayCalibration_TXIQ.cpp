#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>

// External references to objects defined in MainBoard_Display.cpp
extern RA8875 tft;

///////////////////////////////////////////////////////////////////////////////
// TRANSMIT IQ CALIBRATION SECTION
///////////////////////////////////////////////////////////////////////////////
//
// TX IQ calibration minimizes transmitted opposite sideband by adjusting:
// - IQXAmpCorrectionFactor: Transmit I/Q amplitude balance
// - IQXPhaseCorrectionFactor: Transmit I/Q phase balance
//
// Calibration procedure:
// 1. Connect radio transmitter output to calibrated receiver
// 2. Generate SSB tone (LSB or USB)
// 3. Measure opposite sideband suppression visually on receiver
// 4. Adjust TX IQ parameters to minimize unwanted sideband
// 5. Adjust TX attenuation to control power level into receiver
// 6. Repeat for all bands
//
///////////////////////////////////////////////////////////////////////////////
#define TXIQ_SPECTRUM_REFRESH_MS 100
static char buff[100];
static bool redrawSpectrum;
static uint8_t incindex = 0;
const float32_t incvals[] = {0.01, 0.001};
static float32_t increment = incvals[incindex];
float32_t attLevel = 0.0;

static const int8_t NUMBER_OF_TXIQ_PANES = 8;
// Forward declaration of the pane drawing functions
static void DrawTXIQDeltaPane(void);
static void DrawTXIQAtt(void);
static void DrawTXIQStatus(void);
static void DrawTXIQFrequency(void);
static void DrawTXIQAdjustPane(void);
static void DrawTXIQTablePane(void);
static void DrawTXIQInstructionsPane(void);
static void DrawTXIQSpectrumPane(void);

// Pane instances
static Pane PaneTXIQDelta =    {250,45,160,40,DrawTXIQDeltaPane,1};
static Pane PaneTXIQAtt =      {660,330,120,40,DrawTXIQAtt,1};
static Pane PaneTXIQStatus =   {660,380,120,40,DrawTXIQStatus,1};
static Pane PaneTXIQFrequency ={660,430,140,40,DrawTXIQFrequency,1};
static Pane PaneTXIQAdjust =   {3,250,300,230,DrawTXIQAdjustPane,1};
static Pane PaneTXIQTable =    {320,250,200,230,DrawTXIQTablePane,1};
static Pane PaneTXIQInstructions = {537,7,260,320,DrawTXIQInstructionsPane,1};
static Pane PaneTXIQSpectrum = {3,95,517,150,DrawTXIQSpectrumPane,1};

// Array of all panes for iteration
static Pane* TXIQWindowPanes[NUMBER_OF_TXIQ_PANES] = {&PaneTXIQAdjust,&PaneTXIQTable,
                                    &PaneTXIQInstructions, &PaneTXIQAtt, &PaneTXIQDelta,
                                    &PaneTXIQStatus,&PaneTXIQFrequency,&PaneTXIQSpectrum};


extern struct dispSc displayScale[];


static float32_t oldsep = 0.0;
/**
 * @brief Render the sideband separation (Delta) display pane
 * @note Shows measured dB difference between desired and unwanted sideband
 * @note Higher values indicate better IQ balance (target > 60 dB)
 */
static void DrawTXIQDeltaPane(void){
    if (!HasDualVFOs())
        return; // This only works if we have dual VFOs
    if (oldsep != GetTXDeltaVals(ED.currentBand[ED.activeVFO]))
        PaneTXIQDelta.stale = true;
    oldsep = GetTXDeltaVals(ED.currentBand[ED.activeVFO]);

    if (!PaneTXIQDelta.stale) return;
    tft.fillRect(PaneTXIQDelta.x0, PaneTXIQDelta.y0, PaneTXIQDelta.width, PaneTXIQDelta.height, RA8875_BLACK);
    
    sprintf(buff,"%2.1fdB",GetTXDeltaVals(ED.currentBand[ED.activeVFO]));
    tft.setCursor(PaneTXIQDelta.x0, PaneTXIQDelta.y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.print(buff);

    PaneTXIQDelta.stale = false;
}

/**
 * Calculate vertical pixel position for a spectrum FFT bin.
 */
FASTRUN static int16_t pixeln(uint32_t i){
    int16_t zeroPoint = -1*(int16_t)((-124.0 - RECEIVE_POWER_OFFSET)/10.0*displayScale[ED.spectrumScale].dBScale);
    int16_t result = zeroPoint+(int16_t)(displayScale[0].dBScale * psdnew[i]); // 20dB scale
    return result;
}

static const uint16_t MAX_WATERFALL_WIDTH = SPECTRUM_RES;
static const uint16_t SPECTRUM_LEFT_X = PaneTXIQSpectrum.x0+2; 
static const uint16_t SPECTRUM_TOP_Y  = PaneTXIQSpectrum.y0;
static const uint16_t SPECTRUM_HEIGHT = PaneTXIQSpectrum.height;

static uint16_t pixelold[MAX_WATERFALL_WIDTH];
static int16_t x1 = 0;
static int16_t y_left;
static int16_t y_prev = pixelold[0];
static int16_t offset = (SPECTRUM_TOP_Y+SPECTRUM_HEIGHT-10);
static int16_t y_current = offset;
#define WIN_WIDTH 32
#define DARK_RED tft.Color565(64, 0, 0)
static int16_t centerLine = (MAX_WATERFALL_WIDTH + SPECTRUM_LEFT_X) / 2;

/**
 * @brief Render TX IQ calibration spectrum display
 * @note Shows two highlighted regions for upper/lower sideband measurement
 * @note Blue region = desired sideband, dark red = unwanted sideband
 * @note Measures sideband separation and updates Delta display
 * @note Runs in real-time from ISR when new PSD data available
 */
FASTRUN void PlotTXIQSpectrum(void){
    offset = (SPECTRUM_TOP_Y+SPECTRUM_HEIGHT-ED.spectrumNoiseFloor[ED.currentBand[ED.activeVFO]]);
    // Draw the lower sideband
    x1 = MAX_WATERFALL_WIDTH/2-WIN_WIDTH;
    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB)
        tft.fillRect(SPECTRUM_LEFT_X+x1,SPECTRUM_TOP_Y,WIN_WIDTH,SPECTRUM_HEIGHT,RA8875_BLUE);
    else
        tft.fillRect(SPECTRUM_LEFT_X+x1,SPECTRUM_TOP_Y,WIN_WIDTH,SPECTRUM_HEIGHT,DARK_RED);
    for (int j = 0; j < WIN_WIDTH; j++){
        y_left = y_current;
        y_current = offset - pixeln(x1);
        if (y_current > SPECTRUM_TOP_Y+SPECTRUM_HEIGHT) y_current = SPECTRUM_TOP_Y+SPECTRUM_HEIGHT;
        if (y_current < SPECTRUM_TOP_Y) y_current = SPECTRUM_TOP_Y;
        tft.drawLine(SPECTRUM_LEFT_X+x1, y_prev, SPECTRUM_LEFT_X+x1, pixelold[x1], RA8875_BLACK);
        tft.drawLine(SPECTRUM_LEFT_X+x1, y_left, SPECTRUM_LEFT_X+x1, y_current, RA8875_YELLOW);
        y_prev = pixelold[x1];
        pixelold[x1] = y_current;
        x1++;
    }
    // Draw the upper sideband
    x1 = MAX_WATERFALL_WIDTH/2;
    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB)
        tft.fillRect(SPECTRUM_LEFT_X+x1,SPECTRUM_TOP_Y,WIN_WIDTH,SPECTRUM_HEIGHT,DARK_RED);
    else
        tft.fillRect(SPECTRUM_LEFT_X+x1,SPECTRUM_TOP_Y,WIN_WIDTH,SPECTRUM_HEIGHT,RA8875_BLUE);
    for (int j = 0; j < WIN_WIDTH; j++){
        y_left = y_current;
        y_current = offset - pixeln(x1);
        if (y_current > SPECTRUM_TOP_Y+SPECTRUM_HEIGHT) y_current = SPECTRUM_TOP_Y+SPECTRUM_HEIGHT;
        if (y_current < SPECTRUM_TOP_Y) y_current = SPECTRUM_TOP_Y;

        tft.drawLine(SPECTRUM_LEFT_X+x1, y_prev, SPECTRUM_LEFT_X+x1, pixelold[x1], RA8875_BLACK);
        tft.drawLine(SPECTRUM_LEFT_X+x1, y_left, SPECTRUM_LEFT_X+x1, y_current, RA8875_YELLOW);
        y_prev = pixelold[x1];
        pixelold[x1] = y_current;
        x1++;
    }
    psdupdated = false;
}

/**
 * @brief Render the TX IQ calibration spectrum pane
 * @note Calls PlotTXIQSpectrum() when new PSD data available
 */
static void DrawTXIQSpectrumPane(void){
    // Only draw the spectrum if we have dual VFOs and updated spectrum
    // is available
    if (psdupdated && HasDualVFOs() && redrawSpectrum){
        PlotTXIQSpectrum();
        redrawSpectrum = false;
    }
}

float32_t oldatt = -5.0;
/**
 * @brief Render the TX attenuation display pane
 * @note Shows current transmit attenuation for power control during calibration
 */
static void DrawTXIQAtt(void){
    if (oldatt != attLevel) 
        PaneTXIQAtt.stale = true;
    oldatt = attLevel;
    if (!PaneTXIQAtt.stale) return;
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    tft.setTextColor(RA8875_WHITE);

    tft.fillRect(PaneTXIQAtt.x0-tft.getFontWidth()*15, PaneTXIQAtt.y0, PaneTXIQAtt.width+tft.getFontWidth()*15, PaneTXIQAtt.height, RA8875_BLACK);

    tft.setCursor(PaneTXIQAtt.x0,PaneTXIQAtt.y0);
    tft.print(attLevel);
    tft.setCursor(PaneTXIQAtt.x0-tft.getFontWidth()*15,PaneTXIQAtt.y0);
    tft.print("Transmit Att.:");

    PaneTXIQAtt.stale = false;
}

static ModeSm_StateId oldstate = ModeSm_StateId_ROOT;
/**
 * @brief Render the transmit on/off status display pane
 * @note Shows "On" (red) when transmitting, "Off" (green) when not
 * @note TX toggles on/off automatically during TX IQ calibration
 */
static void DrawTXIQStatus(void){
    if (oldstate != modeSM.state_id) 
        PaneTXIQStatus.stale = true;
    oldstate = modeSM.state_id;
    if (!PaneTXIQStatus.stale) return;
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    tft.setTextColor(RA8875_WHITE);

    tft.fillRect(PaneTXIQStatus.x0-tft.getFontWidth()*10, PaneTXIQStatus.y0, PaneTXIQStatus.width+tft.getFontWidth()*10, PaneTXIQStatus.height, RA8875_BLACK);

    tft.setCursor(PaneTXIQStatus.x0,PaneTXIQStatus.y0);
    switch(modeSM.state_id){
        case ModeSm_StateId_CALIBRATE_TX_IQ_SPACE:
            tft.setTextColor(RA8875_GREEN);
            tft.print("Off");
            break;
        case ModeSm_StateId_CALIBRATE_TX_IQ_MARK:
            tft.setTextColor(RA8875_RED);
            tft.print("On");
            break;
        default:
            break;
    }
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneTXIQStatus.x0-tft.getFontWidth()*10,PaneTXIQStatus.y0);
    tft.print("Transmit:");

    PaneTXIQStatus.stale = false;
}

int64_t oldfreq = 0;
/**
 * @brief Render the transmit frequency display pane
 * @note Shows current TX frequency in kHz
 * @note Frequency is band center during TX IQ calibration
 */
static void DrawTXIQFrequency(void){
    int64_t freq = GetTXRXFreq(ED.activeVFO);
    if (oldfreq != freq) 
        PaneTXIQFrequency.stale = true;
    oldfreq = freq;
    if (!PaneTXIQFrequency.stale) return;
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    tft.setTextColor(RA8875_WHITE);

    tft.fillRect(PaneTXIQFrequency.x0-tft.getFontWidth()*11, PaneTXIQFrequency.y0, PaneTXIQFrequency.width+tft.getFontWidth()*11, PaneTXIQFrequency.height, RA8875_BLACK);

    tft.setCursor(PaneTXIQFrequency.x0,PaneTXIQFrequency.y0);
    tft.print(freq/1000);
    tft.print("kHz");
    tft.setCursor(PaneTXIQFrequency.x0-tft.getFontWidth()*11,PaneTXIQFrequency.y0);
    tft.print("Frequency:");

    PaneTXIQFrequency.stale = false;
}


static uint8_t incindexTXIQ = 0;
/**
 * @brief Toggle TX IQ calibration adjustment increment
 * @note Switches between 0.01 and 0.001 step sizes
 * @note Called when user presses button 15
 */
void ChangeTXIQIncrement(void){
    incindexTXIQ++;
    if (incindexTXIQ >= sizeof(incvals)/sizeof(incvals[0]))
        incindexTXIQ = 0;
    increment = incvals[incindexTXIQ];
}

/**
 * @brief Increase transmit attenuation by 0.5 dB
 * @note Clamped to maximum 31.5 dB
 * @note Called when user rotates finetune encoder clockwise
 */
void IncrementTransmitAtt(void){
    attLevel += 0.5;
    if (attLevel > 31.5)
        attLevel = 31.5;
    SetTXAttenuation(attLevel);
    ForceUpdateRFHardwareState();
}

/**
 * @brief Decrease transmit attenuation by 0.5 dB
 * @note Clamped to minimum 0.0 dB
 * @note Called when user rotates finetune encoder counter-clockwise
 */
void DecrementTransmitAtt(void){
    attLevel -= 0.5;
    if (attLevel < 0.0)
        attLevel = 0.0;
    SetTXAttenuation(attLevel);
    ForceUpdateRFHardwareState();
}

/**
 * @brief Increase TX IQ phase correction factor for current band
 * @note Increments by current increment value, clamped to [-0.5, 0.5]
 * @note Called when user rotates filter encoder clockwise
 */
void IncrementTXIQPhase(void){
    ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] += increment;
    if (ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] > 0.5)
        ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = 0.5;
}
/**
 * @brief Decrease TX IQ phase correction factor for current band
 * @note Decrements by current increment value, clamped to [-0.5, 0.5]
 * @note Called when user rotates filter encoder counter-clockwise
 */
void DecrementTXIQPhase(void){
    ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] -= increment;
    if (ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] < -0.5)
        ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = -0.5;
}

/**
 * @brief Increase TX IQ amplitude correction factor for current band
 * @note Increments by current increment value, clamped to [0.5, 2.5]
 * @note Called when user rotates volume encoder clockwise
 */
void IncrementTXIQAmp(void){
    ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] += increment;
    if (ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] > 2.5)
        ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = 2.5;
}

/**
 * @brief Decrease TX IQ amplitude correction factor for current band
 * @note Decrements by current increment value, clamped to [0.5, 2.0]
 * @note Called when user rotates volume encoder counter-clockwise
 */
void DecrementTXIQAmp(void){
    ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] -= increment;
    if (ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] < 0.5)
        ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = 0.5;
}

int8_t oldTXIQincind = 5;
int8_t oldTXIQband = -1;
float32_t oldTXIQamp = -5.0;
float32_t oldTXIQphase = -5.0;
/**
 * @brief Render the TX IQ current band adjustment values pane
 * @note Shows band name, amplitude, phase, and increment values
 * @note Updates when user adjusts parameters or changes bands
 */
static void DrawTXIQAdjustPane(void){
    if ((oldTXIQincind != incindexTXIQ) ||
        (oldTXIQband != ED.currentBand[ED.activeVFO]) ||
        (oldTXIQamp != ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]]) ||
        (oldTXIQphase != ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]]))
        PaneTXIQAdjust.stale = true;
    oldTXIQincind = incindexTXIQ;
    oldTXIQband = ED.currentBand[ED.activeVFO];
    oldTXIQamp = ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]];
    oldTXIQphase = ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]];

    if (!PaneTXIQAdjust.stale) return;
    tft.fillRect(PaneTXIQAdjust.x0, PaneTXIQAdjust.y0, PaneTXIQAdjust.width, PaneTXIQAdjust.height, RA8875_BLACK);
    tft.drawRect(PaneTXIQAdjust.x0, PaneTXIQAdjust.y0, PaneTXIQAdjust.width, PaneTXIQAdjust.height, RA8875_YELLOW);
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.setCursor(PaneTXIQAdjust.x0+3,PaneTXIQAdjust.y0+3);
    tft.print("Current Band");
    
    tft.setCursor(PaneTXIQAdjust.x0+3,PaneTXIQAdjust.y0+3+40);
    tft.print("Band:");
    tft.setCursor(PaneTXIQAdjust.x0+3+120,PaneTXIQAdjust.y0+3+40);
    tft.print(bands[ED.currentBand[ED.activeVFO]].name);
    
    tft.setCursor(PaneTXIQAdjust.x0+3,PaneTXIQAdjust.y0+3+40*2);
    tft.print("Amp:");
    tft.setCursor(PaneTXIQAdjust.x0+3+120,PaneTXIQAdjust.y0+3+40*2);
    sprintf(buff,"%4.3f",ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]]);
    tft.print(buff);

    tft.setCursor(PaneTXIQAdjust.x0+3,PaneTXIQAdjust.y0+3+40*3);
    tft.print("Phase:");
    tft.setCursor(PaneTXIQAdjust.x0+3+120,PaneTXIQAdjust.y0+3+40*3);
    sprintf(buff,"%4.3f",ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]]);
    tft.print(buff);
    
    tft.setCursor(PaneTXIQAdjust.x0+3,PaneTXIQAdjust.y0+3+40*4);
    tft.print("Increment:");
    tft.setCursor(PaneTXIQAdjust.x0+3+180,PaneTXIQAdjust.y0+3+40*4);
    sprintf(buff,"%4.3f",increment);
    tft.print(buff);

    PaneTXIQAdjust.stale = false;
}

/**
 * @brief Calculate sum of TX IQ amplitude correction factors across all bands
 * @return Sum of absolute amplitude correction values
 * @note Used to detect changes requiring table pane redraw
 */
float32_t GetTXIQAmpSum(void){
    float32_t ampsum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        ampsum += abs(ED.IQXAmpCorrectionFactor[k]);
    }
    return ampsum;
}

/**
 * @brief Calculate sum of TX IQ phase correction factors across all bands
 * @return Sum of absolute phase correction values
 * @note Used to detect changes requiring table pane redraw
 */
float32_t GetTXIQPhsSum(void){
    float32_t phssum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        phssum += abs(ED.IQXPhaseCorrectionFactor[k]);
    }
    return phssum;
}

static float32_t oldTXIQampsums = 0;
static float32_t oldTXIQphssums = -10;
/**
 * @brief Render the TX IQ all-bands calibration summary table
 * @note Shows amplitude and phase values for all bands
 * @note Helps user track calibration progress across bands
 */
static void DrawTXIQTablePane(void){
    float32_t nas = GetTXIQAmpSum();
    float32_t nps = GetTXIQPhsSum();
    if ((oldTXIQampsums != nas) || (oldTXIQphssums != nps))
        PaneTXIQTable.stale = true;
    oldTXIQampsums = nas;
    oldTXIQphssums = nps;
    if (!PaneTXIQTable.stale) return;

    tft.fillRect(PaneTXIQTable.x0, PaneTXIQTable.y0, PaneTXIQTable.width, PaneTXIQTable.height, RA8875_BLACK);
    tft.drawRect(PaneTXIQTable.x0, PaneTXIQTable.y0, PaneTXIQTable.width, PaneTXIQTable.height, RA8875_YELLOW);
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);

    tft.setCursor(PaneTXIQTable.x0+5, PaneTXIQTable.y0+3);
    tft.print("Band");
    tft.setCursor(PaneTXIQTable.x0+50, PaneTXIQTable.y0+3);
    tft.print("Amp");
    tft.setCursor(PaneTXIQTable.x0+100, PaneTXIQTable.y0+3);
    tft.print("Phs");
    if (HasDualVFOs()){
        tft.setCursor(PaneTXIQTable.x0+160, PaneTXIQTable.y0+3);
        tft.print("Val");
    }

    for (size_t k=FIRST_BAND; k<=LAST_BAND; k++){
        int16_t y = PaneTXIQTable.y0 + 20 + (k - FIRST_BAND)*17;
        tft.setCursor(PaneTXIQTable.x0+5, y);
        tft.print(bands[k].name);

        tft.setCursor(PaneTXIQTable.x0+50, y);
        sprintf(buff,"%4.3f",ED.IQXAmpCorrectionFactor[k]);
        tft.print(buff);
        
        tft.setCursor(PaneTXIQTable.x0+100, y);
        sprintf(buff,"%4.3f",ED.IQXPhaseCorrectionFactor[k]);
        tft.print(buff);   

        if (HasDualVFOs()){
            if (GetTXDeltaVals(k) != 0.0){
                tft.setCursor(PaneTXIQTable.x0+160, y);
                sprintf(buff,"%2.1f",GetTXDeltaVals(k));
                tft.print(buff);
            }
        }
    }
    PaneTXIQTable.stale = false;
}

/**
 * @brief Render the TX IQ calibration instructions pane
 * @note Displays step-by-step calibration procedure
 * @note Explains encoder controls and external receiver requirement
 */
static void DrawTXIQInstructionsPane(void){
    if (!PaneTXIQInstructions.stale) return;
    tft.fillRect(PaneTXIQInstructions.x0, PaneTXIQInstructions.y0, PaneTXIQInstructions.width, PaneTXIQInstructions.height, RA8875_BLACK);
    int16_t x0 = PaneTXIQInstructions.x0;
    int16_t y0 = PaneTXIQInstructions.y0;

    tft.setCursor(x0, y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.print("Instructions");

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    int16_t delta = 40;
    int16_t lineD = 20;
    tft.setCursor(x0, y0+delta);
    tft.print("* Turn the volume knob to");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    adjust amp");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("* Turn the filter knob to");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    adjust phase");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("* Press button 15 to change");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    the increment");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print(" * Adjust until Delta > 60 dB");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print(" * Press Band Up or Band Down");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    to change to the next band.");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print(" * Turn finetune knob to change");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    TX attenuation if needed.");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print(" * Press Home to save and exit.");

    PaneTXIQInstructions.stale = false;
}

// State tracking for periodic display updates
static uint32_t timerDisplay_ms = 0;

/**
 * @brief Main TX IQ calibration screen rendering function
 * @note Called from DrawDisplay() when in CALIBRATE_TX_IQ UI state
 * @note User manually adjusts TX IQ parameters while monitoring external receiver
 * @note TX automatically toggles on/off to generate test signal
 */
void DrawCalibrateTXIQ(void){
    if (uiSM.vars.clearScreen){
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)1);
        tft.setCursor(10,10);
        tft.print("Transmit IQ calibration");
        if (HasDualVFOs()){
            tft.setCursor(120,  PaneTXIQDelta.y0);
            tft.print("Delta:");
        }
        ED.centerFreq_Hz[ED.activeVFO] = (bands[ED.currentBand[ED.activeVFO]].fBandHigh_Hz+bands[ED.currentBand[ED.activeVFO]].fBandLow_Hz)/2 + SR[SampleRate].rate/4;
        ED.fineTuneFreq_Hz[ED.activeVFO] = 0;
        ED.modulation[ED.activeVFO] = bands[ED.currentBand[ED.activeVFO]].mode;

        // Set the initial attenuation to 0
        attLevel = 0.0;
        SetTXAttenuation(attLevel);
        ForceUpdateRFHardwareState();

        // Mark all the panes stale to force a screen refresh
        for (size_t i = 0; i < NUMBER_OF_TXIQ_PANES; i++){
            TXIQWindowPanes[i]->stale = true;
        }

    }

    if (millis()-timerDisplay_ms > TXIQ_SPECTRUM_REFRESH_MS) {
        timerDisplay_ms = millis();
        if (redrawSpectrum == false)
            redrawSpectrum = true;
    }

    for (size_t i = 0; i < NUMBER_OF_TXIQ_PANES; i++){
        TXIQWindowPanes[i]->DrawFunction();
    }
}
