#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>

// External references to objects defined in MainBoard_Display.cpp
extern RA8875 tft;

///////////////////////////////////////////////////////////////////////////////
// POWER CALIBRATION SECTION
///////////////////////////////////////////////////////////////////////////////

static const int8_t NUMBER_OF_POWER_PANES = 6;
// Forward declaration of the pane drawing functions
static void DrawPowerAttPane(void);
static void DrawPowerDataPane(void);
static void DrawPowerPowerPane(void);
static void DrawPowerAdjustPane(void);
static void DrawPowerTablePane(void);
static void DrawPowerInstructionsPane(void);

// Pane instances
static Pane PanePowerAtt =      {310,50,100,40,DrawPowerAttPane,1};
static Pane PanePowerPower =    {310,100,220,40,DrawPowerPowerPane,1};
static Pane PanePowerData =     {290,150,230,90,DrawPowerDataPane,1};
static Pane PanePowerAdjust =   {3,250,300,230,DrawPowerAdjustPane,1};
static Pane PanePowerTable =    {300,250,210,230,DrawPowerTablePane,1};
static Pane PanePowerInstructions = {530,7,260,470,DrawPowerInstructionsPane,1};

// Array of all panes for iteration
static Pane* PowerWindowPanes[NUMBER_OF_POWER_PANES] = {&PanePowerAdjust,&PanePowerTable,
                                    &PanePowerInstructions, &PanePowerAtt,
                                    &PanePowerData, &PanePowerPower};

static char buff[100];

float32_t oldpowdatasum = 0.0;
/**
 * Draw the data points accumulated so far for the power curve fit
 */
static void DrawPowerDataPane(void){
    if ((oldpowdatasum != GetPowDataSum()))
        PanePowerData.stale = true;
    oldpowdatasum = GetPowDataSum();

    if (!PanePowerData.stale) return;
    tft.fillRect(PanePowerData.x0, PanePowerData.y0, PanePowerData.width, PanePowerData.height, RA8875_BLACK);
    //tft.drawRect(PanePowerData.x0, PanePowerData.y0, PanePowerData.width, PanePowerData.height, RA8875_YELLOW);

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    
    const int16_t col1x = 5;
    const int16_t col2x = 35;
    const int16_t col3x = 90;
    const int16_t col4x = 145;
    
    tft.setCursor(PanePowerData.x0+col2x, PanePowerData.y0+3);
    tft.print("Step");
    tft.setCursor(PanePowerData.x0+col3x, PanePowerData.y0+3);
    tft.print("Atten");
    tft.setCursor(PanePowerData.x0+col4x, PanePowerData.y0+3);
    tft.print("Power");
    tft.drawFastHLine(PanePowerData.x0+30,PanePowerData.y0+72,180,RA8875_WHITE);
    int16_t y;

    // Draw the arrow to indicate which measurement we're updating and check marks
    // for the completed measurements
    switch (powerSM.state_id){
        case PowerCalSm_StateId_POWERPOINT1:{
            y = PanePowerData.y0 + 20 + 0*17;
            tft.setCursor(PanePowerData.x0+col1x, y);
            tft.print(">"); // right arrow character
            break;
        }
        case PowerCalSm_StateId_POWERPOINT2:{
            y = PanePowerData.y0 + 20 + 1*17;
            tft.setCursor(PanePowerData.x0+col1x, y);
            tft.print(">"); // right arrow character
            // Mark the prior rows as complete
            for (size_t k=0; k<1; k++){
                y = PanePowerData.y0 + 20 + k*17;
                tft.setCursor(PanePowerData.x0+col1x, y);
                // Draw a check mark
                tft.setTextColor(RA8875_GREEN);
                tft.print("v"); // Checkmark character
                tft.setTextColor(RA8875_WHITE);
            }
            break;
        }
        case PowerCalSm_StateId_POWERPOINT3:{
            y = PanePowerData.y0 + 20 + 2*17;
            tft.setCursor(PanePowerData.x0+col1x, y);
            tft.print(">"); // right arrow character
            for (size_t k=0; k<2; k++){
                y = PanePowerData.y0 + 20 + k*17;
                tft.setCursor(PanePowerData.x0+col1x, y);
                // Draw a check mark
                tft.setTextColor(RA8875_GREEN);
                tft.print("v"); // Checkmark character
                tft.setTextColor(RA8875_WHITE);
            }
            break;
        }
        case PowerCalSm_StateId_SSBPOINT:{
            y = PanePowerData.y0 + 20 + 3*17;
            tft.setCursor(PanePowerData.x0+col1x, y);
            tft.print(">"); // right arrow character
            for (size_t k=0; k<3; k++){
                y = PanePowerData.y0 + 20 + k*17;
                tft.setCursor(PanePowerData.x0+col1x, y);
                // Draw a check mark
                tft.setTextColor(RA8875_GREEN);
                tft.print("v"); // Checkmark character
                tft.setTextColor(RA8875_WHITE);
            }
            break;
        }
        case PowerCalSm_StateId_POWERCOMPLETE:{
            for (size_t k=0; k<3; k++){
                y = PanePowerData.y0 + 20 + k*17;
                tft.setCursor(PanePowerData.x0+col1x, y);
                // Draw a check mark
                tft.setTextColor(RA8875_GREEN);
                tft.print("v"); // Checkmark character
                tft.setTextColor(RA8875_WHITE);
            }
            break;
        }
        case PowerCalSm_StateId_MEASUREMENTCOMPLETE:{
            for (size_t k=0; k<4; k++){
                y = PanePowerData.y0 + 20 + k*17;
                tft.setCursor(PanePowerData.x0+col1x, y);
                // Draw a check mark
                tft.setTextColor(RA8875_GREEN);
                tft.print("v"); // Checkmark character
                tft.setTextColor(RA8875_WHITE);
            }
            break;
        }
        default:
            break;
    }

    // Draw the steps and the curve fit data points captured so far
    for (size_t k=0; k<4; k++){
        y = PanePowerData.y0 + 20 + k*17;
        tft.setCursor(PanePowerData.x0+col2x, y);
        tft.print((int32_t)k+1);
        if (k < GetNpoints()){
            // Draw attenuation and power for points 1 to 3
            tft.setCursor(PanePowerData.x0+col3x, y);
            tft.print(GetAttenuation_dB(k));

            tft.setCursor(PanePowerData.x0+col4x, y);
            sprintf(buff,"%3.2f",GetPower_W(k));
            tft.print(buff);
        }
    }
    // Draw the SSB power point if we have completed the measurements
    if (powerSM.state_id == PowerCalSm_StateId_MEASUREMENTCOMPLETE){
        tft.setCursor(PanePowerData.x0+col3x, y);
        tft.print(0);
        tft.setCursor(PanePowerData.x0+col4x, y);
        sprintf(buff,"%3.2f",GetSSBPower_W());
        tft.print(buff);
    }
    PanePowerData.stale = false;
}

uint8_t incindexPower = 1;
const float32_t powerincvals[] = {1, 0.1, 0.01};

/**
 * @brief Toggle power calibration adjustment increment
 * @note Switches between 0.1 and 0.01 step sizes
 * @note Called when user presses button 15
 */
void ChangePowerIncrement(void){
    incindexPower++;
    if (incindexPower >= sizeof(powerincvals)/sizeof(powerincvals[0]))
        incindexPower = 0;
}

void IncrementCalibrationPower(void){
    float32_t p = GetMeasuredPower();
    p += powerincvals[incindexPower];
    if (p > 100.0)
        p = 100.0;
    SetMeasuredPower(p);
}

void DecrementCalibrationPower(void){
    float32_t p = GetMeasuredPower();
    p -= powerincvals[incindexPower];
    if (p < 0.0)
        p = 0.0;
    SetMeasuredPower(p);
}

float32_t oldpow = -5.0;
float32_t oldtargetPower = 0.0;
/**
 * @brief Render the power display pane
 * @note Shows measured power during calibration
 */
static void DrawPowerPowerPane(void){
    if ((oldpow != GetMeasuredPower()) || (oldtargetPower != GetTargetPower()) ) 
        PanePowerPower.stale = true;
    oldpow = GetMeasuredPower();
    oldtargetPower = GetTargetPower();
    if (!PanePowerPower.stale) return;
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);

    tft.fillRect(PanePowerPower.x0-tft.getFontWidth()*7, PanePowerPower.y0, PanePowerPower.width+tft.getFontWidth()*7, PanePowerPower.height, RA8875_BLACK);

    tft.setCursor(PanePowerPower.x0,PanePowerPower.y0);
    if (GetPowerUnit())
        sprintf(buff,"%3.2fW ",oldpow);
    else
        sprintf(buff,"%2.1fdBm ",oldpow);
    tft.print(buff);
    tft.setTextColor(RA8875_MAGENTA);
    sprintf(buff,"%3.2f",oldtargetPower);
    tft.print(buff);
    tft.setTextColor(RA8875_WHITE);

    tft.setCursor(PanePowerPower.x0-tft.getFontWidth()*7,PanePowerPower.y0);
    tft.print("Power:");

    PanePowerPower.stale = false;
}


float32_t oldpowatt = -5.0;
/**
 * @brief Render the power attenuation display pane
 * @note Shows current attenuation for power control during calibration
 */
static void DrawPowerAttPane(void){
    if (oldpowatt != ED.XAttenCW[ED.currentBand[ED.activeVFO]]) 
        PanePowerAtt.stale = true;
    oldpowatt = ED.XAttenCW[ED.currentBand[ED.activeVFO]];
    if (!PanePowerAtt.stale) return;
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);

    tft.fillRect(PanePowerAtt.x0-tft.getFontWidth()*13, PanePowerAtt.y0, PanePowerAtt.width+tft.getFontWidth()*13, PanePowerAtt.height, RA8875_BLACK);

    tft.setCursor(PanePowerAtt.x0,PanePowerAtt.y0);
    tft.print(ED.XAttenCW[ED.currentBand[ED.activeVFO]]);
    tft.setCursor(PanePowerAtt.x0-tft.getFontWidth()*13,PanePowerAtt.y0);
    tft.print("Attenuation:");
    PanePowerAtt.stale = false;
}

static int8_t oldPincind = -1;
static int32_t oldPband = 754;
static bool oldpasel = 5;
static uint8_t oldpu = 3;
static ModeSm_StateId oldmode = ModeSm_StateId_NORMAL_STATES;
/**
 * @brief Render the power current band adjustment values pane
 * @note Shows PA selection, band name, frequency, transmit stats, increment value
 * @note Updates when user adjusts parameters or changes bands
 */
static void DrawPowerAdjustPane(void){
    if ((oldPincind != incindexPower) || 
        (oldPband != ED.currentBand[ED.activeVFO]) || 
        (oldpasel != ED.PA100Wactive) ||
        (oldmode != modeSM.state_id) ||
        (oldpu != GetPowerUnit()))
        PanePowerAdjust.stale = true;
    oldPincind = incindexPower;
    oldPband = ED.currentBand[ED.activeVFO];
    oldpasel = ED.PA100Wactive;
    oldmode = modeSM.state_id;
    oldpu = GetPowerUnit();

    if (!PanePowerAdjust.stale) return;
    tft.fillRect(PanePowerAdjust.x0, PanePowerAdjust.y0, PanePowerAdjust.width, PanePowerAdjust.height, RA8875_BLACK);
    //tft.drawRect(PanePowerAdjust.x0, PanePowerAdjust.y0, PanePowerAdjust.width, PanePowerAdjust.height, RA8875_YELLOW);
    
    int16_t x0 = PanePowerAdjust.x0+3;
    int16_t y0 = PanePowerAdjust.y0+3;
    int16_t delta = 0;
    int16_t lineD = 35;

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    
    tft.setCursor(x0,y0+delta);
    tft.print("PA:");
    tft.setCursor(x0+120,y0+delta);
    if (ED.PA100Wactive)
        tft.print("100W");
    else
        tft.print("20W");

    delta += lineD;
    tft.setCursor(x0,y0+delta);
    tft.print("Band:");
    tft.setCursor(x0+120,y0+delta);
    tft.print(bands[ED.currentBand[ED.activeVFO]].name);
    
    delta += lineD;
    tft.setCursor(x0,y0+delta);
    tft.print("Freq:");
    tft.setCursor(x0+120,y0+delta);
    sprintf(buff,"%lldkHz",GetTXRXFreq(ED.activeVFO)/1000);
    tft.print(buff);

    delta += lineD;
    tft.setCursor(x0,y0+delta);
    tft.print("Transmit:");
    tft.setCursor(x0+160,y0+delta);
    if (modeSM.state_id == ModeSm_StateId_CALIBRATE_TX_IQ_MARK){
        tft.setTextColor(RA8875_RED);
        tft.print("On");
    } else {
        tft.setTextColor(RA8875_GREEN);
        tft.print("Off");
    }
    tft.setTextColor(RA8875_WHITE);

    delta += lineD;
    tft.setCursor(x0,y0+delta);
    tft.print("Increment:");
    tft.setCursor(x0+160,y0+delta);
    sprintf(buff,"%3.2f",powerincvals[incindexPower]);
    tft.print(buff);

    delta += lineD;
    tft.setCursor(x0,y0+delta);
    tft.print("Units:");
    tft.setCursor(x0+160,y0+delta);
    if (oldpu)
        tft.print("W");
    else
        tft.print("dBm");
    PanePowerAdjust.stale = false;
}

/**
 * @brief Calculate sum of Psat factors across all bands
 * @return Sum of PSat values
 * @note Used to detect changes requiring table pane redraw
 */
float32_t GetPsatSum(void){
    float32_t psatsum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        if (ED.PA100Wactive)
            psatsum += abs(ED.PowerCal_100W_Psat_mW[k]);
        else
            psatsum += abs(ED.PowerCal_20W_Psat_mW[k]);
    }
    return psatsum;
}

/**
 * @brief Calculate sum of TX IQ phase correction factors across all bands
 * @return Sum of absolute phase correction values
 * @note Used to detect changes requiring table pane redraw
 */
float32_t GetKSum(void){
    float32_t ksum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        if (ED.PA100Wactive)
            ksum += abs(ED.PowerCal_100W_kindex[k]);
        else
            ksum += abs(ED.PowerCal_20W_kindex[k]);
    }
    return ksum;
}

float32_t GetCorrSum(void){
    float32_t ksum = 0;
    for (size_t k=0; k<NUMBER_OF_BANDS; k++){
        if (ED.PA100Wactive)
            ksum += abs(ED.PowerCal_100W_DSP_Gain_correction_dB[k]);
        else
            ksum += abs(ED.PowerCal_20W_DSP_Gain_correction_dB[k]);
    }
    return ksum;
}

static float32_t oldPsatsum = 0;
static float32_t oldksum = 0;
static float32_t oldcorrsum = -1;
/**
 * @brief Render the power all-bands calibration summary table
 * @note Shows Psat and kindex values for all bands
 * @note Helps user track calibration progress across bands
 */
static void DrawPowerTablePane(void){
    float32_t nps = GetPsatSum();
    float32_t nks = GetKSum();
    float32_t ncs = GetCorrSum();
    if ((oldPsatsum != nps) || (oldksum != nks) || (oldcorrsum != ncs))
        PanePowerTable.stale = true;
    oldPsatsum = nps;
    oldksum = nks;
    oldcorrsum = ncs;
    if (!PanePowerTable.stale) return;

    tft.fillRect(PanePowerTable.x0, PanePowerTable.y0, PanePowerTable.width, PanePowerTable.height, RA8875_BLACK);
    //tft.drawRect(PanePowerTable.x0, PanePowerTable.y0, PanePowerTable.width, PanePowerTable.height, RA8875_YELLOW);
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);

    tft.setCursor(PanePowerTable.x0+5, PanePowerTable.y0+3);
    tft.print("Band");
    tft.setCursor(PanePowerTable.x0+50, PanePowerTable.y0+3);
    tft.print("Psat");
    tft.setCursor(PanePowerTable.x0+120, PanePowerTable.y0+3);
    tft.print("k");
    tft.setCursor(PanePowerTable.x0+170, PanePowerTable.y0+3);
    tft.print("Gain");


    for (size_t k=FIRST_BAND; k<=LAST_BAND; k++){
        int16_t y = PanePowerTable.y0 + 20 + (k - FIRST_BAND)*17;
        tft.setCursor(PanePowerTable.x0+5, y);
        tft.print(bands[k].name);

        tft.setCursor(PanePowerTable.x0+50, y);
        if (ED.PA100Wactive)
            sprintf(buff,"%2.1f",ED.PowerCal_100W_Psat_mW[k]);
        else
            sprintf(buff,"%2.1f",ED.PowerCal_20W_Psat_mW[k]);

        tft.print(buff);
        
        tft.setCursor(PanePowerTable.x0+120, y);
        if (ED.PA100Wactive)
            sprintf(buff,"%2.1f",ED.PowerCal_100W_kindex[k]);
        else
            sprintf(buff,"%2.1f",ED.PowerCal_20W_kindex[k]);
        tft.print(buff);

        tft.setCursor(PanePowerTable.x0+170, y);
        if (ED.PA100Wactive)
            sprintf(buff,"%2.1f",ED.PowerCal_100W_DSP_Gain_correction_dB[k]);
        else
            sprintf(buff,"%2.1f",ED.PowerCal_20W_DSP_Gain_correction_dB[k]);
        tft.print(buff);

    }
    PanePowerTable.stale = false;
}

/**
 * @brief Render the power calibration instructions pane
 * @note Displays step-by-step calibration procedure
 * @note Shows green checkmarks for completed steps
 */
static void DrawPowerInstructionsPane(void){
    if (!PanePowerInstructions.stale) return;
    tft.fillRect(PanePowerInstructions.x0, PanePowerInstructions.y0, PanePowerInstructions.width, PanePowerInstructions.height, RA8875_BLACK);
    int16_t x0 = PanePowerInstructions.x0;
    int16_t y0 = PanePowerInstructions.y0;

    tft.setCursor(x0, y0);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.print("Instructions");

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    int16_t delta = 40;
    int16_t lineD = 20;

    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("1-Record power level at 0dB atten");

    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("2-Adjust atten to drop pow by 6dB");

    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("3-Adjust atten to drop power by");
    
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("  a further 6dB");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("4-Record measured power");
    
    delta += 2*lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("Record actual power at each step");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("by pressing SELECT(0) button.");
    

    delta += 2*lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("After step 3, press button 12 to");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("proceed to step 4, or press ZOOM");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("to re-measure data points.");


    delta += 2*lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("* Volume encoder adjusts atten.");

    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("* Filter encoder adjusts power.");

    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("* Zoom resets back to step 1.");

    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("* Button 14 changes W/dBm choice.");

    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("* Button 15 changes increment.");

    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("* Button 12 changes measure mode.");

    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("* Button 16 changes PA selection.");

    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("* Press Home to save and exit.");

    PanePowerInstructions.stale = false;
}

/**
 * @brief Main power calibration screen rendering function
 * @note Called from DrawDisplay() when in CALIBRATE_POWER UI state
 * @note Calibrates power adjustment and measurement circuitry
 */
void DrawCalibratePower(void){
        
    if (uiSM.vars.clearScreen){
        Debug("Entry to CALIBRATE_POWER state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)1);
        tft.setCursor(10,10);
        tft.print("Power calibration");

        // Mark all the panes stale to force a screen refresh
        for (size_t i = 0; i < NUMBER_OF_POWER_PANES; i++){
            PowerWindowPanes[i]->stale = true;
        }

    }

    for (size_t i = 0; i < NUMBER_OF_POWER_PANES; i++){
        PowerWindowPanes[i]->DrawFunction();
    }
}
