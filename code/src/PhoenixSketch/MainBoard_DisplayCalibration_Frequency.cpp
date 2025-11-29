#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>

// External references to objects defined in MainBoard_Display.cpp
extern RA8875 tft;

///////////////////////////////////////////////////////////////////////////////
// FREQUENCY CALIBRATION SECTION
///////////////////////////////////////////////////////////////////////////////
//
// Frequency calibration corrects the reference oscillator error by:
// 1. Tuning to a known reference signal (e.g., WWV, CHU)
// 2. Using SAM (Synchronous AM) demodulator to measure carrier offset
// 3. Adjusting freqCorrectionFactor until SAM error approaches zero
// 4. Correction factor is applied to all VFO frequency calculations
//
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


/**
 * @brief Render frequency calibration plot pane (placeholder)
 * @note Reserved for future spectrum/trend plotting
 */
static void DrawFreqPlotPane(void){
    // blank for now
}

static int32_t ofcf = -100000;
/**
 * @brief Render the frequency correction factor display pane
 * @note Shows current freqCorrectionFactor value
 * @note Updates when user adjusts correction via filter encoder
 */
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

// Frequency correction increment values (in Hz)
const int32_t freqIncrements[] = {1,10,100,1000,10000};
static uint8_t freqIncrementIndex = 1;

/**
 * @brief Cycle through frequency correction factor increment values
 * @note Toggles through: 1, 10, 100, 1000, 10000 Hz
 * @note Called when user presses button 15
 */
void ChangeFrequencyCorrectionFactorIncrement(void){
    freqIncrementIndex++;
    if (freqIncrementIndex >= sizeof(freqIncrements)/sizeof(freqIncrements[0]))
        freqIncrementIndex = 0;
}

/**
 * @brief Increase frequency correction factor
 * @note Increments by current increment value
 * @note Immediately applies correction to Si5351 VFO
 * @note Called when user rotates filter encoder clockwise
 */
void IncreaseFrequencyCorrectionFactor(void){
    ED.freqCorrectionFactor += freqIncrements[freqIncrementIndex];
    SetFrequencyCorrectionFactor(ED.freqCorrectionFactor);
}

/**
 * @brief Decrease frequency correction factor
 * @note Decrements by current increment value
 * @note Immediately applies correction to Si5351 VFO
 * @note Called when user rotates filter encoder counter-clockwise
 */
void DecreaseFrequencyCorrectionFactor(void){
    ED.freqCorrectionFactor -= freqIncrements[freqIncrementIndex];
    SetFrequencyCorrectionFactor(ED.freqCorrectionFactor);
}

static int32_t offi = -100000;
/**
 * @brief Render the frequency correction increment display pane
 * @note Shows current adjustment increment (1, 10, 100, 1000, or 10000 Hz)
 */
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
/**
 * @brief Render the modulation mode display pane
 * @note Shows current modulation (should be SAM for frequency calibration)
 * @note SAM shown in green (correct), other modes in red (incorrect)
 */
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
/**
 * @brief Render the SAM carrier offset error display pane
 * @note Shows SAM demodulator carrier frequency error in Hz
 * @note Target is to minimize this value (ideally < 1 Hz)
 */
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

/**
 * @brief Render the frequency calibration instructions pane
 * @note Displays step-by-step calibration procedure
 * @note Reminds user to use SAM mode and minimize error value
 */
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

/**
 * @brief Main frequency calibration screen rendering function
 * @note Called from DrawDisplay() when in CALIBRATE_FREQUENCY UI state
 * @note User adjusts freqCorrectionFactor while monitoring SAM error
 */
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
