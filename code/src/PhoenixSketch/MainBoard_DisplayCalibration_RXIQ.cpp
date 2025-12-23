#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>

// External references to objects defined in MainBoard_Display.cpp
extern RA8875 tft;


///////////////////////////////////////////////////////////////////////////////
// RECEIVE IQ CALIBRATION SECTION
///////////////////////////////////////////////////////////////////////////////
//
// RX IQ calibration minimizes image rejection by adjusting:
// - IQAmpCorrectionFactor: Amplitude imbalance between I and Q channels
// - IQPhaseCorrectionFactor: Phase error between I and Q channels
//
// Calibration procedure:
// 1. Generate CW tone offset by 48 kHz from LO
// 2. Measure desired sideband level vs unwanted sideband level
// 3. Adjust amplitude/phase to maximize sideband separation (Delta)
// 4. Target Delta > 60 dB for good image rejection
// 5. Repeat for all bands
//
// Optional auto-tune algorithm performs systematic parameter sweep
//
///////////////////////////////////////////////////////////////////////////////
static const int8_t NUMBER_OF_PANES = 5;
// Forward declaration of the pane drawing functions
static void DrawDeltaPane(void);
static void DrawAdjustPane(void);
static void DrawTablePane(void);
static void DrawInstructionsPane(void);
static void DrawSpectrumPane(void);

// Pane instances
static Pane PaneDelta =    {250,45,160,40,DrawDeltaPane,1};
static Pane PaneAdjust =   {3,250,300,230,DrawAdjustPane,1};
static Pane PaneTable =    {320,250,200,230,DrawTablePane,1};
static Pane PaneInstructions = {537,7,260,470,DrawInstructionsPane,1};
static Pane PaneSpectrum = {3,95,517,150,DrawSpectrumPane,1};

// Array of all panes for iteration
static Pane* WindowPanes[NUMBER_OF_PANES] = {&PaneDelta,&PaneAdjust,&PaneTable,
                                    &PaneSpectrum,&PaneInstructions};

extern struct dispSc displayScale[];

/**
 * Calculate vertical pixel position for a spectrum FFT bin.
 */
FASTRUN static int16_t pixeln(uint32_t i){
    int16_t zeroPoint = -1*(int16_t)((-124.0 - RECEIVE_POWER_OFFSET)/10.0*displayScale[ED.spectrumScale].dBScale);
    int16_t result = zeroPoint+(int16_t)(displayScale[0].dBScale * psdnew[i]); // 20dB scale
    return result;
}

static const uint16_t MAX_WATERFALL_WIDTH = SPECTRUM_RES;
static const uint16_t SPECTRUM_LEFT_X = PaneSpectrum.x0+2; 
static const uint16_t SPECTRUM_TOP_Y  = PaneSpectrum.y0;
static const uint16_t SPECTRUM_HEIGHT = PaneSpectrum.height;

static uint16_t pixelold[MAX_WATERFALL_WIDTH];
static int16_t x1 = 0;
static int16_t y_left;
static int16_t y_prev = pixelold[0];
static int16_t offset = (SPECTRUM_TOP_Y+SPECTRUM_HEIGHT-30);
static int16_t y_current = offset;
#define WIN_WIDTH 20
#define DARK_RED tft.Color565(64, 0, 0)
static char buff[100];
static int16_t centerLine = (MAX_WATERFALL_WIDTH + SPECTRUM_LEFT_X) / 2;
//static int32_t Nreadings = 0;                  // Reading counter for measurement averaging

/**
 * @brief Render RX IQ calibration spectrum display
 * @note Shows two highlighted regions for upper/lower sideband measurement
 * @note Blue region = desired sideband, dark red = unwanted sideband
 * @note Measures sideband separation and updates Delta display
 * @note Runs in real-time from ISR when new PSD data available
 */
FASTRUN void PlotSpectrum(void){
    offset = (SPECTRUM_TOP_Y+SPECTRUM_HEIGHT-ED.spectrumNoiseFloor[ED.currentBand[ED.activeVFO]]);

    x1 = MAX_WATERFALL_WIDTH/4-WIN_WIDTH/2;
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

    x1 = MAX_WATERFALL_WIDTH*3/4-WIN_WIDTH/2;
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
    psdupdated = false;
}

// RX IQ adjustment increment values
static uint8_t incindex = 0;
const float32_t incvals[] = {0.01, 0.001};
static float32_t increment = incvals[incindex];

/**
 * @brief Toggle RX IQ calibration adjustment increment
 * @note Switches between 0.01 and 0.001 step sizes
 * @note Called when user presses button 15
 */
void ChangeRXIQIncrement(void){
    incindex++;
    if (incindex >= sizeof(incvals)/sizeof(incvals[0]))
        incindex = 0;
    increment = incvals[incindex];
}


static float32_t oldsep = 0.0;
/**
 * @brief Render the sideband separation (Delta) display pane
 * @note Shows measured dB difference between desired and unwanted sideband
 * @note Higher values indicate better IQ balance (target > 60 dB)
 */
static void DrawDeltaPane(void){
    if (oldsep != GetRXDeltaVals(ED.currentBand[ED.activeVFO]))
        PaneDelta.stale = true;
    oldsep = GetRXDeltaVals(ED.currentBand[ED.activeVFO]);

    if (!PaneDelta.stale) return;
    tft.fillRect(PaneDelta.x0, PaneDelta.y0, PaneDelta.width, PaneDelta.height, RA8875_BLACK);
    
    sprintf(buff,"%2.1fdB",GetRXDeltaVals(ED.currentBand[ED.activeVFO]));
    tft.setCursor(PaneDelta.x0, PaneDelta.y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.print(buff);

    PaneDelta.stale = false;
}


/**
 * @brief Increase RX IQ phase correction factor for current band
 * @note Increments by current increment value, clamped to [-0.5, 0.5]
 * @note Called when user rotates filter encoder clockwise
 */
void IncrementRXIQPhase(void){
    ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] += increment;
    if (ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] > 0.5)
        ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = 0.5;
}

/**
 * @brief Decrease RX IQ phase correction factor for current band
 * @note Decrements by current increment value, clamped to [-0.5, 0.5]
 * @note Called when user rotates filter encoder counter-clockwise
 */
void DecrementRXIQPhase(void){
    ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] -= increment;
    if (ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] < -0.5)
        ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = -0.5;
}

/**
 * @brief Increase RX IQ amplitude correction factor for current band
 * @note Increments by current increment value, clamped to [0.5, 2.0]
 * @note Called when user rotates volume encoder clockwise
 */
void IncrementRXIQAmp(void){
    ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] += increment;
    if (ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] > 2.0)
        ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = 2.0;
}

/**
 * @brief Decrease RX IQ amplitude correction factor for current band
 * @note Decrements by current increment value, clamped to [0.5, 2.0]
 * @note Called when user rotates volume encoder counter-clockwise
 */
void DecrementRXIQAmp(void){
    ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] -= increment;
    if (ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] < 0.5)
        ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = 0.5;
}

int8_t oldincind = 5;
float32_t oldamp = -5.0;
float32_t oldphase = -5.0;
/**
 * @brief Render the RX IQ current band adjustment values pane
 * @note Shows band name, amplitude, phase, and increment values
 * @note Updates when user adjusts parameters or changes bands
 */
static void DrawAdjustPane(void){
    if ((oldincind != incindex) || 
        (oldamp != ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]]) || 
        (oldphase != ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]]))
        PaneAdjust.stale = true;
    oldincind = incindex;
    oldamp = ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]];
    oldphase = ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]];

    if (!PaneAdjust.stale) return;
    tft.fillRect(PaneAdjust.x0, PaneAdjust.y0, PaneAdjust.width, PaneAdjust.height, RA8875_BLACK);
    tft.drawRect(PaneAdjust.x0, PaneAdjust.y0, PaneAdjust.width, PaneAdjust.height, RA8875_YELLOW);
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.setCursor(PaneAdjust.x0+3,PaneAdjust.y0+3);
    tft.print("Current Band");
    
    tft.setCursor(PaneAdjust.x0+3,PaneAdjust.y0+3+40);
    tft.print("Band:");
    tft.setCursor(PaneAdjust.x0+3+120,PaneAdjust.y0+3+40);
    tft.print(bands[ED.currentBand[ED.activeVFO]].name);
    
    tft.setCursor(PaneAdjust.x0+3,PaneAdjust.y0+3+40*2);
    tft.print("Amp:");
    tft.setCursor(PaneAdjust.x0+3+120,PaneAdjust.y0+3+40*2);
    sprintf(buff,"%4.3f",ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]]);
    tft.print(buff);

    tft.setCursor(PaneAdjust.x0+3,PaneAdjust.y0+3+40*3);
    tft.print("Phase:");
    tft.setCursor(PaneAdjust.x0+3+120,PaneAdjust.y0+3+40*3);
    sprintf(buff,"%4.3f",ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]]);
    tft.print(buff);
    
    tft.setCursor(PaneAdjust.x0+3,PaneAdjust.y0+3+40*4);
    tft.print("Increment:");
    tft.setCursor(PaneAdjust.x0+3+180,PaneAdjust.y0+3+40*4);
    sprintf(buff,"%4.3f",increment);
    tft.print(buff);

    PaneAdjust.stale = false;
}

/**
 * @brief Calculate sum of RX IQ amplitude correction factors across all bands
 * @return Sum of absolute amplitude correction values
 * @note Used to detect changes requiring table pane redraw
 */
float32_t GetAmpSum(void){
    float32_t ampsum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        ampsum += abs(ED.IQAmpCorrectionFactor[k]);
    }
    return ampsum;
}

/**
 * @brief Calculate sum of RX IQ phase correction factors across all bands
 * @return Sum of absolute phase correction values
 * @note Used to detect changes requiring table pane redraw
 */
float32_t GetPhsSum(void){
    float32_t phssum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        phssum += abs(ED.IQPhaseCorrectionFactor[k]);
    }
    return phssum;
}

static float32_t oldampsums = 0;
static float32_t oldphssums = -10;
/**
 * @brief Render the RX IQ all-bands calibration summary table
 * @note Shows amplitude, phase, and Delta values for all bands
 * @note Helps user track calibration progress across bands
 */
static void DrawTablePane(void){
    float32_t nas = GetAmpSum();
    float32_t nps = GetPhsSum();
    if ((oldampsums != nas) || (oldphssums != nps))
        PaneTable.stale = true;
    oldampsums = nas;
    oldphssums = nps;
    if (!PaneTable.stale) return;

    tft.fillRect(PaneTable.x0, PaneTable.y0, PaneTable.width, PaneTable.height, RA8875_BLACK);
    tft.drawRect(PaneTable.x0, PaneTable.y0, PaneTable.width, PaneTable.height, RA8875_YELLOW);
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);

    tft.setCursor(PaneTable.x0+5, PaneTable.y0+3);
    tft.print("Band");
    tft.setCursor(PaneTable.x0+50, PaneTable.y0+3);
    tft.print("Amp");
    tft.setCursor(PaneTable.x0+100, PaneTable.y0+3);
    tft.print("Phs");
    tft.setCursor(PaneTable.x0+160, PaneTable.y0+3);
    tft.print("Val");

    for (size_t k=FIRST_BAND; k<=LAST_BAND; k++){
        int16_t y = PaneTable.y0 + 20 + (k - FIRST_BAND)*17;
        tft.setCursor(PaneTable.x0+5, y);
        tft.print(bands[k].name);

        tft.setCursor(PaneTable.x0+50, y);
        sprintf(buff,"%4.3f",ED.IQAmpCorrectionFactor[k]);
        tft.print(buff);
        
        tft.setCursor(PaneTable.x0+100, y);
        sprintf(buff,"%4.3f",ED.IQPhaseCorrectionFactor[k]);
        tft.print(buff);        

        if (GetRXDeltaVals(k) != 0.0){
            tft.setCursor(PaneTable.x0+160, y);
            sprintf(buff,"%2.1f",GetRXDeltaVals(k));
            tft.print(buff);
        }
    }
    PaneTable.stale = false;
}

/**
 * @brief Render the RX IQ calibration instructions pane
 * @note Displays step-by-step calibration procedure
 * @note Explains encoder controls and target Delta value
 */
static void DrawInstructionsPane(void){
    if (!PaneInstructions.stale) return;
    tft.fillRect(PaneInstructions.x0, PaneInstructions.y0, PaneInstructions.width, PaneInstructions.height, RA8875_BLACK);
    int16_t x0 = PaneInstructions.x0;
    int16_t y0 = PaneInstructions.y0;

    tft.setCursor(x0, y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.print("Instructions");

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    int16_t delta = 40;
    int16_t lineD = 20;
    tft.setCursor(x0, y0+delta);
    tft.print("* Press button 16 for auto.");
    delta += lineD;
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
    tft.print(" * Press Home to save and exit.");

    PaneInstructions.stale = false;
}

/**
 * @brief Render the RX IQ calibration spectrum pane
 * @note Calls PlotSpectrum() when new PSD data available
 */
static void DrawSpectrumPane(void){
    if (psdupdated){
        PlotSpectrum();
    }
}

/**
 * @brief Main RX IQ calibration screen rendering function
 * @note Called from DrawDisplay() when in CALIBRATE_RX_IQ UI state
 * @note User manually adjusts amp/phase or runs auto-tune algorithm
 * @note Displays real-time spectrum with sideband separation measurement
 */
void DrawCalibrateRXIQ(void){
    if (uiSM.vars.clearScreen){
        Debug("Entry to CALIBRATE_RXIQ state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
        tft.setCursor(10,10);
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)1);
        tft.print("Receive IQ calibration");
        tft.drawRect(PaneSpectrum.x0,PaneSpectrum.y0,PaneSpectrum.width,PaneSpectrum.height,RA8875_YELLOW);
        tft.setCursor(120,  PaneDelta.y0);
        tft.print("Delta:");
        // Mark all the panes stale to force a screen refresh
        for (size_t i = 0; i < NUMBER_OF_PANES; i++){
            WindowPanes[i]->stale = true;
        }
    }

    for (size_t i = 0; i < NUMBER_OF_PANES; i++){
        WindowPanes[i]->DrawFunction();
    }
   
}