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

///////////////////////////////////////////////////////////////////////////////
// Frequency calibration section
///////////////////////////////////////////////////////////////////////////////

static const int8_t NUMBER_OF_FREQ_PANES = 6;
// Forward declaration of the pane drawing functions
static void DrawFreqPlotPane(void);
static void DrawFreqFactorPane(void);
static void DrawFreqFactorIncrPane(void);
static void DrawFreqErrorPane(void);
static void DrawFreqInstructionsPane(void);
static void DrawFreqModulationPane(void);

// Pane instances
static Pane PaneFreqPlot =   {3,95,517,150,DrawFreqPlotPane,1};
static Pane PaneFreqFactor = {140,270,120,40,DrawFreqFactorPane,1};
static Pane PaneFreqFactorIncr = {140,330,120,40,DrawFreqFactorIncrPane,1};
static Pane PaneFreqError =  {390,270,120,40,DrawFreqErrorPane,1};
static Pane PaneFreqMod =    {390,330,120,40,DrawFreqModulationPane,1};
static Pane PaneFreqInstructions = {537,7,260,470,DrawFreqInstructionsPane,1};

// Array of all panes for iteration
static Pane* FreqWindowPanes[NUMBER_OF_FREQ_PANES] = {&PaneFreqPlot,&PaneFreqFactor,&PaneFreqFactorIncr,
                                    &PaneFreqError,&PaneFreqInstructions,&PaneFreqMod};


static void DrawFreqPlotPane(void){
    // blank for now
}

static int32_t ofcf = -100000;
static void DrawFreqFactorPane(void){
    if (ofcf != ED.freqCorrectionFactor)
        PaneFreqFactor.stale = true;
    ofcf = ED.freqCorrectionFactor;

    if (!PaneFreqFactor.stale) return;
    tft.fillRect(PaneFreqFactor.x0, PaneFreqFactor.y0, PaneFreqFactor.width, PaneFreqFactor.height, RA8875_BLACK);
    
    tft.setCursor(PaneFreqFactor.x0, PaneFreqFactor.y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.print(ED.freqCorrectionFactor);

    PaneFreqFactor.stale = false;
}

const int32_t freqIncrements[] = {1,10,100,1000,10000};
static uint8_t freqIncrementIndex = 1;

void ChangeFrequencyCorrectionFactorIncrement(void){
    freqIncrementIndex++;
    if (freqIncrementIndex > sizeof(freqIncrements)/sizeof(freqIncrements[0]))
        freqIncrementIndex = 0;
}

void IncreaseFrequencyCorrectionFactor(void){
    ED.freqCorrectionFactor += freqIncrements[freqIncrementIndex];
    SetFrequencyCorrectionFactor(ED.freqCorrectionFactor);
}

void DecreaseFrequencyCorrectionFactor(void){
    ED.freqCorrectionFactor -= freqIncrements[freqIncrementIndex];
    SetFrequencyCorrectionFactor(ED.freqCorrectionFactor);
}

static int32_t offi = -100000;
static void DrawFreqFactorIncrPane(void){
    if (offi != freqIncrements[freqIncrementIndex])
        PaneFreqFactorIncr.stale = true;
    offi = freqIncrements[freqIncrementIndex];

    if (!PaneFreqFactorIncr.stale) return;
    tft.fillRect(PaneFreqFactorIncr.x0, PaneFreqFactorIncr.y0, PaneFreqFactorIncr.width, PaneFreqFactorIncr.height, RA8875_BLACK);
    
    tft.setCursor(PaneFreqFactorIncr.x0, PaneFreqFactorIncr.y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.print(freqIncrements[freqIncrementIndex]);

    PaneFreqFactorIncr.stale = false;
}

static ModulationType omod = DCF77;
static void DrawFreqModulationPane(void){
    if (omod != ED.modulation[ED.activeVFO])
        PaneFreqMod.stale = true;
    omod = ED.modulation[ED.activeVFO];

    if (!PaneFreqMod.stale) return;
    tft.fillRect(PaneFreqMod.x0, PaneFreqMod.y0, PaneFreqMod.width, PaneFreqMod.height, RA8875_BLACK);
    
    tft.setCursor(PaneFreqMod.x0, PaneFreqMod.y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    switch (ED.modulation[ED.activeVFO]){
        case LSB:
            tft.setTextColor(RA8875_RED);
            tft.print("LSB");
            break;
        case USB:
            tft.setTextColor(RA8875_RED);
            tft.print("USB");
            break;
        case AM:
            tft.setTextColor(RA8875_RED);
            tft.print("AM");
            break;
        case SAM:
            tft.setTextColor(RA8875_GREEN);
            tft.print("SAM");
            break;
        default:
            break;
    }
    PaneFreqMod.stale = false;
}

static float32_t ofe = -100000.0;
static void DrawFreqErrorPane(void){
    float32_t SAMOffset = GetSAMCarrierOffset();
    if (ofe != SAMOffset)
        PaneFreqError.stale = true;
    ofe = SAMOffset;

    if (!PaneFreqError.stale) return;
    tft.fillRect(PaneFreqError.x0, PaneFreqError.y0, PaneFreqError.width, PaneFreqError.height, RA8875_BLACK);
    char buff[20];
    sprintf(buff,"%2.1f",SAMOffset);
    tft.setCursor(PaneFreqError.x0, PaneFreqError.y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.print(buff);

    PaneFreqError.stale = false;
}

static void DrawFreqInstructionsPane(void){
    if (!PaneFreqInstructions.stale) return;
    tft.fillRect(PaneFreqInstructions.x0, PaneFreqInstructions.y0, PaneFreqInstructions.width, PaneFreqInstructions.height, RA8875_BLACK);
    int16_t x0 = PaneFreqInstructions.x0;
    int16_t y0 = PaneFreqInstructions.y0;
    
    tft.setCursor(x0, y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.print("Instructions");

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    int16_t delta = 40;
    int16_t lineD = 20;
    tft.setCursor(x0, y0+delta);
    tft.print("* Tune to reference signal before");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    engaging this calibration.");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("* Make sure modulation is SAM.");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("* Turn filter encoder to adjust");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    the correction factor.");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("* Press button 15 to change");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    the increment if needed.");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print(" * Adjust until error < 1.");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print(" * Press Home to save and exit.");

    PaneFreqInstructions.stale = false;
}

void DrawCalibrateFrequency(void){
    if (uiSM.vars.clearScreen){
        Debug("Entry to CALIBRATE_FREQUENCY state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        
        tft.setCursor(10,10);
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)1);
        tft.print("Frequency calibration");
        tft.setCursor(PaneFreqFactor.x0-tft.getFontWidth()*8,  PaneFreqFactor.y0);
        tft.print("Factor:");
        tft.setCursor(PaneFreqFactorIncr.x0-tft.getFontWidth()*7,  PaneFreqFactorIncr.y0);
        tft.print("Incr.:");
        tft.setCursor(PaneFreqError.x0-tft.getFontWidth()*7,  PaneFreqError.y0);
        tft.print("Error:");

        // Mark all the panes stale to force a screen refresh
        for (size_t i = 0; i < NUMBER_OF_FREQ_PANES; i++){
            FreqWindowPanes[i]->stale = true;
        }
        
        uiSM.vars.clearScreen = false;
    }

    for (size_t i = 0; i < NUMBER_OF_FREQ_PANES; i++){
        FreqWindowPanes[i]->DrawFunction();
    }

}

///////////////////////////////////////////////////////////////////////////////
// Receive IQ calibration section
///////////////////////////////////////////////////////////////////////////////
static bool autotune = false;
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
char buff[100];
int16_t centerLine = (MAX_WATERFALL_WIDTH + SPECTRUM_LEFT_X) / 2;
static float32_t deltaVals[NUMBER_OF_BANDS];
static int32_t Nreadings = 0;

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

    // Because we set the CW tone to be 48 kHz above or below the LO, the upper
    // and lower sideband products will be in very specific bins. Upper will be
    // in bin 3/4*512 = 384, lower will be in bin 1/4*512 = 128
    float32_t upper = psdnew[384]; 
    float32_t lower = psdnew[128]; 
    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB){
        sideband_separation = (upper-lower)*10;
    } else {
        sideband_separation = (lower-upper)*10;
    }
    deltaVals[ED.currentBand[ED.activeVFO]] = 0.5*deltaVals[ED.currentBand[ED.activeVFO]]+0.5*sideband_separation;
    Nreadings++;
    psdupdated = false;
}

static uint8_t incindex = 1;
const float32_t incvals[] = {0.01, 0.001};
static float32_t increment = incvals[incindex];
void ChangeRXIQIncrement(void){
    incindex++;
    if (incindex > sizeof(incvals)/sizeof(incvals[0])) 
        incindex = 0;
    increment = incvals[incindex];
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

// Used to detect a change in any of the parameters to know whether the table should be updated
float32_t GetAmpSum(void){
    float32_t ampsum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        ampsum += abs(ED.IQAmpCorrectionFactor[k]);
    }
    return ampsum;
}

float32_t GetPhsSum(void){
    float32_t phssum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        phssum += abs(ED.IQPhaseCorrectionFactor[k]);
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

        if (deltaVals[k] != 0.0){
            tft.setCursor(PaneTable.x0+160, y);
            sprintf(buff,"%2.1f",deltaVals[k]);
            tft.print(buff);
        }
    }
    PaneTable.stale = false;
}

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

static void DrawSpectrumPane(void){
    if (psdupdated){
        PlotSpectrum();
    }
}

void EngageRXIQAutotune(void){
    autotune = true;
}

// Pass 1
//  Gain in 0.01 steps from 0.5 to 1.5            (iteration 1)
//  Phase changes in 0.01 steps from -0.2 to 0.2  (iteration 2)
// Pass 2
//  Gain in 0.01 steps from 4 steps below previous minimum to 4 steps above   (iteration 3)
//  phase in 0.01 steps from 4 steps below previous minimum to 4 steps above  (iteration 4)
// Pass 3
//  Gain in 0.001 steps 10 steps below to 10 steps above   (iteration 5)
//  Phase in 0.001 steps 10 steps below to 10 steps above  (iteration 6)
float32_t center[] ={1.0,                  0.0,                  0.0, 0.0, 0.0, 0.0};
int8_t NSteps[]  =  {(int)((1.5-0.5)/0.01),(int)((0.2+0.2)/0.01),9,   21,  9,   21 }; 
float32_t Delta[] = {0.01,                 0.01,                 0.01,0.01,0.001,0.001};
float32_t maxSBS = 0.0;
float32_t maxSBS_parameter = 0.0;
static int8_t iteration = 0;
static int8_t step = 0;
static bool bandCompleted[NUMBER_OF_BANDS]; // all should start as false
static bool initialEntry = false;

float32_t GetNewVal(int8_t iter, int8_t stp){
    float32_t newval = center[iter]-(NSteps[iter]*Delta[iter])/2.0+stp*Delta[iter];
    return newval;
}

void SetAmpPhase(int8_t iter, int8_t stp){
    float32_t newval = GetNewVal(iter, stp);
    Nreadings = 0;
    if (iter%2 == 0){
        ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = newval;
    } else {
        ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = newval;
    }
}
float32_t maxSBS_save;
void TuneIQValues(void){
    // Catch the initial entry condition:
    if (initialEntry){
        Debug("Initial entry to tuning IQ. Setting initial point");
        for (size_t k=0; k<NUMBER_OF_BANDS; k++){
            bandCompleted[k] = false;
        }
        Nreadings = 0;
        ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = 0.0;
        SetAmpPhase(iteration,step);
        initialEntry = false;
        return;
    }
    if (bandCompleted[ED.currentBand[ED.activeVFO]]){
        ED.currentBand[ED.activeVFO]++;
        if (ED.currentBand[ED.activeVFO] > LAST_BAND){
            ED.currentBand[ED.activeVFO] = LAST_BAND;
            autotune = false;
            Debug("Autotune complete!");
            return;
        }
        // Change the band
        ED.centerFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][0];
        ED.fineTuneFreq_Hz[ED.activeVFO] = ED.lastFrequencies[ED.currentBand[ED.activeVFO]][1];
        ED.modulation[ED.activeVFO] = (ModulationType)ED.lastFrequencies[ED.currentBand[ED.activeVFO]][2];
        UpdateRFHardwareState();

        // Start the first iteration for this new band
        iteration = 0;
        step = 0;
        Nreadings = 0;
        maxSBS = 0.0;
        ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = 0.0;
        SetAmpPhase(iteration,step);
    }

    // Once Nreadings reaches 6 the new reading is ready
    if (Nreadings > 6){
        // Save this parameter if the sideband separation is the largest so far
        if (deltaVals[ED.currentBand[ED.activeVFO]] > maxSBS){
            // The value of the sideband separation
            maxSBS = deltaVals[ED.currentBand[ED.activeVFO]];
            // The amp/phase parameter that delivered this sideband separation
            maxSBS_parameter = GetNewVal(iteration, step);
        }

        // Proceed to the next step in this iteration
        step++;
        if (step >= NSteps[iteration]){
            // Set the parameter we were changing to the minimum value
            if (iteration%2 == 0){
                ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = maxSBS_parameter;
            } else {
                ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = maxSBS_parameter;
            }
            // The next time we step around the amplitude or phase, use this as our starting point
            int8_t nextIndex = iteration + 2;
            if (nextIndex < 6)
                center[nextIndex] = maxSBS_parameter;

            // Go to the next iteration
            step = 0;
            iteration++;
            maxSBS_save = maxSBS;
            maxSBS = 0.0;
        }
        if (iteration > 5){
            // Go to next band
            bandCompleted[ED.currentBand[ED.activeVFO]] = true;
            // Set the parameter we were changing to the minimum value
            if ((iteration-1)%2 == 0){
                ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = maxSBS_parameter;
            } else {
                ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = maxSBS_parameter;
            }
            deltaVals[ED.currentBand[ED.activeVFO]] = maxSBS_save;
            return;
        } 
        // Change the appropriate parameter
        SetAmpPhase(iteration,step);
        Nreadings = 0;
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
        // Mark all the panes stale to force a screen refresh
        for (size_t i = 0; i < NUMBER_OF_PANES; i++){
            WindowPanes[i]->stale = true;
        }
        initialEntry = true;
    }

    // If we are in autotune mode, engage the algorithm!
    if (autotune)
        TuneIQValues();

    for (size_t i = 0; i < NUMBER_OF_PANES; i++){
        WindowPanes[i]->DrawFunction();
    }
   
}

///////////////////////////////////////////////////////////////////////////////
// Transmit IQ calibration section
///////////////////////////////////////////////////////////////////////////////

static const int8_t NUMBER_OF_TXIQ_PANES = 5;
// Forward declaration of the pane drawing functions
static void DrawTXIQAtt(void);
static void DrawTXIQStatus(void);
static void DrawTXIQAdjustPane(void);
static void DrawTXIQTablePane(void);
static void DrawTXIQInstructionsPane(void);

// Pane instances
static Pane PaneTXIQAtt =      {310,50,120,40,DrawTXIQAtt,1};
static Pane PaneTXIQStatus =   {310,130,120,40,DrawTXIQStatus,1};
static Pane PaneTXIQAdjust =   {3,250,300,230,DrawTXIQAdjustPane,1};
static Pane PaneTXIQTable =    {320,250,200,230,DrawTXIQTablePane,1};
static Pane PaneTXIQInstructions = {537,7,260,470,DrawTXIQInstructionsPane,1};

// Array of all panes for iteration
static Pane* TXIQWindowPanes[NUMBER_OF_TXIQ_PANES] = {&PaneTXIQAdjust,&PaneTXIQTable,
                                    &PaneTXIQInstructions, &PaneTXIQAtt,
                                    &PaneTXIQStatus};


float32_t oldatt = -5.0;
static void DrawTXIQAtt(void){
    if (oldatt != ED.XAttenSSB[ED.currentBand[ED.activeVFO]]) 
        PaneTXIQAtt.stale = true;
    oldatt = ED.XAttenSSB[ED.currentBand[ED.activeVFO]];
    if (!PaneTXIQAtt.stale) return;
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);

    tft.fillRect(PaneTXIQAtt.x0-tft.getFontWidth()*15, PaneTXIQAtt.y0, PaneTXIQAtt.width+tft.getFontWidth()*15, PaneTXIQAtt.height, RA8875_BLACK);

    tft.setCursor(PaneTXIQAtt.x0,PaneTXIQAtt.y0);
    tft.print(ED.XAttenSSB[ED.currentBand[ED.activeVFO]]);
    tft.setCursor(PaneTXIQAtt.x0-tft.getFontWidth()*15,PaneTXIQAtt.y0);
    tft.print("Transmit Att.:");

    PaneTXIQAtt.stale = false;
}

static ModeSm_StateId oldstate = ModeSm_StateId_ROOT;
static void DrawTXIQStatus(void){
    if (oldstate != modeSM.state_id) 
        PaneTXIQStatus.stale = true;
    oldstate = modeSM.state_id;
    if (!PaneTXIQStatus.stale) return;
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
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
    }
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneTXIQStatus.x0-tft.getFontWidth()*1,PaneTXIQStatus.y0);
    tft.print("Transmit:");

    PaneTXIQStatus.stale = false;
}

static uint8_t incindexTXIQ = 1;
void ChangeTXIQIncrement(void){
    incindexTXIQ++;
    if (incindexTXIQ > sizeof(incvals)/sizeof(incvals[0])) 
        incindexTXIQ = 0;
    increment = incvals[incindexTXIQ];
}

void IncrementTransmitAtt(void){
    ED.XAttenSSB[ED.currentBand[ED.activeVFO]] += 0.5;
    if (ED.XAttenSSB[ED.currentBand[ED.activeVFO]] > 31.5)
        ED.XAttenSSB[ED.currentBand[ED.activeVFO]] = 31.5;
    SetTXAttenuation(ED.XAttenSSB[ED.currentBand[ED.activeVFO]]);
}

void DecrementTransmitAtt(void){
    ED.XAttenSSB[ED.currentBand[ED.activeVFO]] -= 0.5;
    if (ED.XAttenSSB[ED.currentBand[ED.activeVFO]] < 0.0)
        ED.XAttenSSB[ED.currentBand[ED.activeVFO]] = 0.0;
    SetTXAttenuation(ED.XAttenSSB[ED.currentBand[ED.activeVFO]]);
}

void IncrementTXIQPhase(void){
    ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] += increment;
    if (ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] > 0.5)
        ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = 0.5;
}
void DecrementTXIQPhase(void){
    ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] -= increment;
    if (ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] < -0.5)
        ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = -0.5;
}
void IncrementTXIQAmp(void){
    ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] += increment;
    if (ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] > 2.0)
        ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = 2.0;
}
void DecrementTXIQAmp(void){
    ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] -= increment;
    if (ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] < 0.5)
        ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = 0.5;
}

int8_t oldTXIQincind = 5;
float32_t oldTXIQamp = -5.0;
float32_t oldTXIQphase = -5.0;
static void DrawTXIQAdjustPane(void){
    if ((oldTXIQincind != incindexTXIQ) || 
        (oldTXIQamp != ED.IQXAmpCorrectionFactor[ED.currentBand[ED.activeVFO]]) || 
        (oldTXIQphase != ED.IQXPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]]))
        PaneTXIQAdjust.stale = true;
    oldTXIQincind = incindexTXIQ;
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

// Used to detect a change in any of the parameters to know whether the table should be updated
float32_t GetTXIQAmpSum(void){
    float32_t ampsum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        ampsum += abs(ED.IQXAmpCorrectionFactor[k]);
    }
    return ampsum;
}

float32_t GetTXIQPhsSum(void){
    float32_t phssum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        phssum += abs(ED.IQXPhaseCorrectionFactor[k]);
    }
    return phssum;
}

static float32_t oldTXIQampsums = 0;
static float32_t oldTXIQphssums = -10;
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

    }
    PaneTXIQTable.stale = false;
}

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

void DrawCalibrateTXIQ(void){
    if (uiSM.vars.clearScreen){
        Debug("Entry to CALIBRATE_TXIQ state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)1);
        tft.setCursor(10,10);
        tft.print("Transmit IQ calibration");

        // Mark all the panes stale to force a screen refresh
        for (size_t i = 0; i < NUMBER_OF_TXIQ_PANES; i++){
            TXIQWindowPanes[i]->stale = true;
        }

    }

    for (size_t i = 0; i < NUMBER_OF_TXIQ_PANES; i++){
        TXIQWindowPanes[i]->DrawFunction();
    }
}

///////////////////////////////////////////////////////////////////////////////
// Power calibration section
///////////////////////////////////////////////////////////////////////////////

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
