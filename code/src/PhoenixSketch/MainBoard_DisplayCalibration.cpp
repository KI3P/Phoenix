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
char buff[10];
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
    tft.setCursor(PaneTable.x0+55, PaneTable.y0+3);
    tft.print("Amp");
    tft.setCursor(PaneTable.x0+105, PaneTable.y0+3);
    tft.print("Phs");
    tft.setCursor(PaneTable.x0+155, PaneTable.y0+3);
    tft.print("Val");

    for (size_t k=FIRST_BAND; k<=LAST_BAND; k++){
        int16_t y = PaneTable.y0 + 20 + (k - FIRST_BAND)*17;
        tft.setCursor(PaneTable.x0+5, y);
        tft.print(bands[k].name);

        tft.setCursor(PaneTable.x0+55, y);
        tft.print(ED.IQAmpCorrectionFactor[k]);
        
        tft.setCursor(PaneTable.x0+105, y);
        tft.print(ED.IQPhaseCorrectionFactor[k]);
        
        if (deltaVals[k] != 0.0){
            tft.setCursor(PaneTable.x0+155, y);
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
    tft.print("* Press button 16 for auto.");
    delta += lineD;
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
    delta += lineD;
    tft.setCursor(PaneInstructions.x0, PaneInstructions.y0+delta);
    tft.print(" * Press Band Up or Band Down");
    delta += lineD;
    tft.setCursor(PaneInstructions.x0, PaneInstructions.y0+delta);
    tft.print("    to change to the next band.");
    delta += lineD;
    tft.setCursor(PaneInstructions.x0, PaneInstructions.y0+delta);
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
float32_t center[] ={1.0,                  0.0,                  NULL,NULL,NULL,NULL};
int8_t NSteps[]  =  {(int)((1.5-0.5)/0.01),(int)((0.2+0.2)/0.01),9,   21,  9,   21 }; 
float32_t Delta[] = {0.01,                 0.01,                 0.01,0.01,0.001,0.001};
float32_t maxSBS = 0.0;
float32_t maxSBS_parameter = 0.0;
static int8_t iteration = 0;
static int8_t step = 0;
static bool bandCompleted[NUMBER_OF_BANDS]; // all should start as false

float32_t GetNewVal(int8_t iter, int8_t stp){
    float32_t newval = center[iter]-(NSteps[iter]*Delta[iter])/2.0+stp*Delta[iter];
    return newval;
}

void SetAmpPhase(int8_t iter, int8_t stp){
    float32_t newval = GetNewVal(iter, stp);
    sprintf(buff,"Iteration %d, step %d: %4.3f",iter, stp, newval);
    Nreadings = 0;
    Debug(buff);
    if (iter%2 == 0)
        ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = newval;
    else
        ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = newval;
}

void TuneIQValues(void){
    // Catch the initial entry condition:
    if ((ED.currentBand[ED.activeVFO]==FIRST_BAND) && (iteration==0) && (step==0)){
        Debug("Initial entry to tuning IQ. Setting initial point");
        for (size_t k=0; k<NUMBER_OF_BANDS; k++){
            bandCompleted[k] = false;
        }
        Nreadings = 0;
        SetAmpPhase(iteration,step);
    }

    if (bandCompleted[ED.currentBand[ED.activeVFO]]){
        Debug("Incrementing the band");
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
        SetAmpPhase(iteration,step);
    }

    // Once Nreadings reaches 6 the new reading is ready
    if (Nreadings > 6){
        Debug("Reading complete");
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
                sprintf(buff,"Iteration %d: Set amp to %4.3f => SBS = %2.1f",iteration, maxSBS_parameter, maxSBS);
            } else {
                ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = maxSBS_parameter;
                sprintf(buff,"Iteration %d: Set phs to %4.3f => SBS = %2.1f",iteration, maxSBS_parameter, maxSBS);
            }
            // The next time we step around the amplitude or phase, use this as our starting point
            int8_t nextIndex = iteration + 2;
            if (nextIndex < 6)
                center[nextIndex] = maxSBS_parameter;
            Debug(buff);

            // Go to the next iteration
            step = 0;
            iteration++;
            Debug("New iteration" + String(iteration));
        }
        if (iteration > 5){
            Debug("Band complete");
            bandCompleted[ED.currentBand[ED.activeVFO]] = true;
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
    }

    // If we are in autotune mode, engage the algorithm!
    if (autotune)
        TuneIQValues();

    for (size_t i = 0; i < NUMBER_OF_PANES; i++){
        WindowPanes[i]->DrawFunction();
    }
   
}

///////////////////////////////////////////////////////////////////////////////

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
