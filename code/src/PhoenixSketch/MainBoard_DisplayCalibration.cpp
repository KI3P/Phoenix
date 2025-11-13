/**
 * @file MainBoard_DisplayCalibration.cpp
 * @brief Calibration screens for Phoenix SDR radio calibration
 *
 * @see MainBoard_Display.h for menu structure definitions
 * @see MainBoard_Display.cpp for display infrastructure
 */

#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>

// External references to objects defined in MainBoard_Display.cpp
extern RA8875 tft;

void DrawCalibrateFrequency(void){
    if (uiSM.vars.clearScreen){
        Debug("Entry to CALIBRATE_FREQUENCY state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
    }
    tft.setCursor(10,10);
    tft.print("Frequency calibration");
}


static const int8_t NUMBER_OF_PANES = 5;
// Forward declaration of the pane drawing functions
static void DrawDeltaPane(void);
static void DrawAdjustPane(void);
static void DrawTablePane(void);
static void DrawInstructionsPane(void);
static void DrawSpectrumPane(void);

// Pane instances
static Pane PaneDelta =    {250,45,160,40,DrawDeltaPane,1};
static Pane PaneAdjust =   {3,267,300,210,DrawAdjustPane,1};
static Pane PaneTable =    {320,267,200,210,DrawTablePane,1};
static Pane PaneInstructions = {537,7,260,470,DrawInstructionsPane,1};
static Pane PaneSpectrum = {3,95,517,150,DrawSpectrumPane,1};

// Array of all panes for iteration
static Pane* WindowPanes[NUMBER_OF_PANES] = {&PaneDelta,&PaneAdjust,&PaneTable,
                                    &PaneSpectrum,&PaneInstructions};

extern struct dispSc displayScale[];

/**
 * Calculate vertical pixel position for a spectrum FFT bin.
 */
FASTRUN int16_t pixeln(uint32_t i){
    int16_t result = displayScale[0].baseOffset + // 20dB scale
                    20 + // pixeloffset
                    (int16_t)(displayScale[0].dBScale * psdnew[i]); // 20dB scale
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
static float32_t sideband_separation = 0.0;
char buff[10];
int16_t centerLine = (MAX_WATERFALL_WIDTH + SPECTRUM_LEFT_X) / 2;
static float32_t deltaVals[NUMBER_OF_BANDS];

FASTRUN void PlotSpectrum(void){
    offset = (SPECTRUM_TOP_Y+SPECTRUM_HEIGHT-ED.spectrumNoiseFloor[ED.currentBand[ED.activeVFO]]);

    x1 = MAX_WATERFALL_WIDTH/4-WIN_WIDTH/2;
    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB)
        tft.fillRect(SPECTRUM_LEFT_X+x1,SPECTRUM_TOP_Y,WIN_WIDTH,SPECTRUM_HEIGHT,DARK_RED);
    else
        tft.fillRect(SPECTRUM_LEFT_X+x1,SPECTRUM_TOP_Y,WIN_WIDTH,SPECTRUM_HEIGHT,RA8875_BLUE);
    float32_t lowerSBmax = -1000.0;
    for (int j = 0; j < WIN_WIDTH; j++){
        y_left = y_current;
        y_current = offset - pixeln(x1);
        if (y_current > SPECTRUM_TOP_Y+SPECTRUM_HEIGHT) y_current = SPECTRUM_TOP_Y+SPECTRUM_HEIGHT;
        if (y_current < SPECTRUM_TOP_Y) y_current = SPECTRUM_TOP_Y;
        tft.drawLine(SPECTRUM_LEFT_X+x1, y_prev, SPECTRUM_LEFT_X+x1, pixelold[x1], RA8875_BLACK);
        tft.drawLine(SPECTRUM_LEFT_X+x1, y_left, SPECTRUM_LEFT_X+x1, y_current, RA8875_YELLOW);
        y_prev = pixelold[x1];
        pixelold[x1] = y_current;
        if (psdnew[x1] > lowerSBmax) lowerSBmax = psdnew[x1]; 
        x1++;
    }

    x1 = MAX_WATERFALL_WIDTH*3/4-WIN_WIDTH/2;
    float32_t upperSBmax = -1000.0;
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
        if (psdnew[x1] > upperSBmax) upperSBmax = psdnew[x1]; 
        x1++;
    }

    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB)
        sideband_separation = (upperSBmax - lowerSBmax)*10;
    else
        sideband_separation = (lowerSBmax - upperSBmax)*10;

    deltaVals[ED.currentBand[ED.activeVFO]] = sideband_separation;
    psdupdated = false;
}

static uint8_t incindex = 1;
const float32_t incvals[] = {0.1, 0.01, 0.001};
static float32_t increment = incvals[incindex];
void ChangeRXIQIncrement(void){
    incindex++;
    if (incindex > 2) 
        incindex = 0;
    increment = incvals[incindex];
    Debug(String("Change increment to ") + String(increment));
}


static float32_t oldsep = 0.0;
static void DrawDeltaPane(void){
    if (oldsep != deltaVals[ED.currentBand[ED.activeVFO]])
        PaneDelta.stale = true;
    oldsep = deltaVals[ED.currentBand[ED.activeVFO]];

    if (!PaneDelta.stale) return;
    tft.fillRect(PaneDelta.x0, PaneDelta.y0, PaneDelta.width, PaneDelta.height, RA8875_BLACK);
    
    sprintf(buff,"%2.1fdB",deltaVals[ED.currentBand[ED.activeVFO]]);
    tft.setCursor(PaneDelta.x0, PaneDelta.y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.print(buff);

    PaneDelta.stale = false;
}


void IncrementRXIQPhase(void){
    ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] += increment;
    if (ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] > 0.5)
        ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = 0.5;
}
void DecrementRXIQPhase(void){
    ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] -= increment;
    if (ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] < -0.5)
        ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = -0.5;
}
void IncrementRXIQAmp(void){
    ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] += increment;
    if (ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] > 2.0)
        ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = 2.0;
}
void DecrementRXIQAmp(void){
    ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] -= increment;
    if (ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] < 0.5)
        ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = 0.5;
}

int8_t oldincind = 5;
float32_t oldamp = -5.0;
float32_t oldphase = -5.0;
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
    tft.print(ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]]);

    tft.setCursor(PaneAdjust.x0+3,PaneAdjust.y0+3+40*3);
    tft.print("Phase:");
    tft.setCursor(PaneAdjust.x0+3+120,PaneAdjust.y0+3+40*3);
    tft.print(ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]]);
    
    tft.setCursor(PaneAdjust.x0+3,PaneAdjust.y0+3+40*4);
    tft.print("Increment:");
    tft.setCursor(PaneAdjust.x0+3+180,PaneAdjust.y0+3+40*4);
    sprintf(buff,"%4.3f",increment);
    tft.print(buff);

    PaneAdjust.stale = false;
}

float32_t GetAmpSum(void){
    float32_t ampsum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        ampsum += ED.IQAmpCorrectionFactor[k];
    }
    return ampsum;
}

float32_t GetPhsSum(void){
    float32_t phssum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        phssum += ED.IQPhaseCorrectionFactor[k];
    }
    return phssum;
}

static float32_t oldampsums = 0;
static float32_t oldphssums = -10;
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
    tft.setCursor(PaneTable.x0+55, PaneTable.y0+3);
    tft.print("Amp");
    tft.setCursor(PaneTable.x0+105, PaneTable.y0+3);
    tft.print("Phs");
    tft.setCursor(PaneTable.x0+155, PaneTable.y0+3);
    tft.print("Val");

    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        tft.setCursor(PaneTable.x0+5, PaneTable.y0+20+k*17);
        tft.print(bands[k].name);
        tft.setCursor(PaneTable.x0+55, PaneTable.y0+20+k*17);
        tft.print(ED.IQAmpCorrectionFactor[k]);
        tft.setCursor(PaneTable.x0+105, PaneTable.y0+20+k*17);
        tft.print(ED.IQPhaseCorrectionFactor[k]);
        if (deltaVals[k] != 0.0){
            tft.setCursor(PaneTable.x0+155, PaneTable.y0+20+k*17);
            tft.print(deltaVals[k]);
        }
    }
    PaneTable.stale = false;
}

static void DrawInstructionsPane(void){
    //if (stalecondition)
    //    PaneInstructions.stale = true;
    //update stalecondition

    if (!PaneInstructions.stale) return;
    tft.fillRect(PaneInstructions.x0, PaneInstructions.y0, PaneInstructions.width, PaneInstructions.height, RA8875_BLACK);
    
    tft.setCursor(PaneInstructions.x0, PaneInstructions.y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.print("Instructions");

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    int16_t delta = 40;
    int16_t lineD = 20;
    tft.setCursor(PaneInstructions.x0, PaneInstructions.y0+delta);
    tft.print("* Turn the volume knob to");
    delta += lineD;
    tft.setCursor(PaneInstructions.x0, PaneInstructions.y0+delta);
    tft.print("    adjust amp");
    delta += lineD;
    tft.setCursor(PaneInstructions.x0, PaneInstructions.y0+delta);
    tft.print("* Turn the filter knob to");
    delta += lineD;
    tft.setCursor(PaneInstructions.x0, PaneInstructions.y0+delta);
    tft.print("    adjust phase");
    delta += lineD;
    tft.setCursor(PaneInstructions.x0, PaneInstructions.y0+delta);
    tft.print("* Press button 15 to change");
    delta += lineD;
    tft.setCursor(PaneInstructions.x0, PaneInstructions.y0+delta);
    tft.print("    the increment");
    delta += lineD;
    tft.setCursor(PaneInstructions.x0, PaneInstructions.y0+delta);
    tft.print(" * Adjust until Delta > 60 dB");

    PaneInstructions.stale = false;
}

static void DrawSpectrumPane(void){
    if (psdupdated){
        PlotSpectrum();
    }
}

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

        for (size_t i = 0; i < NUMBER_OF_PANES; i++){
            WindowPanes[i]->stale = true;
        }

    }

    for (size_t i = 0; i < NUMBER_OF_PANES; i++){
        WindowPanes[i]->DrawFunction();
    }
   
}

void DrawCalibrateTXIQ(void){
    if (uiSM.vars.clearScreen){
        Debug("Entry to CALIBRATE_TXIQ state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
    }
    tft.setCursor(10,10);
    tft.print("Transmit IQ calibration");
}

void DrawCalibratePower(void){
    if (uiSM.vars.clearScreen){
        Debug("Entry to CALIBRATE_POWER state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
    }
    tft.setCursor(10,10);
    tft.print("Power calibration");
}
