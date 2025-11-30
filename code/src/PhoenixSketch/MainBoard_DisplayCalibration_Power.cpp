#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>

// External references to objects defined in MainBoard_Display.cpp
extern RA8875 tft;

///////////////////////////////////////////////////////////////////////////////
// POWER CALIBRATION SECTION
///////////////////////////////////////////////////////////////////////////////
static char buff[100];

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

//#define PA20W  0
//#define PA100W 1
//int8_t PAselect = PA20W;
float32_t measuredPower = 0.0;
float32_t targetPower = 0.0;
uint8_t powerUnit = 1; // 1=W, 0=dBm

float32_t attenuations_dB[3];
float32_t powers_W[3];
uint32_t Npoints = 0;
uint8_t powerCalibrationStepCount = 0; // Tracks step completion state for instruction display

void CalculatePowerCurveFit(void){
    Debug("Invoked power curve fit");
    float32_t powers_mW[3];
    for (size_t k=0; k<3; k++){
        powers_mW[k] = powers_W[k]*1000.0f;
    }
    struct FitResult f;
    if (ED.PA100Wactive){
        f = FitPowerCurve(attenuations_dB, powers_mW, Npoints, 75000.0f,10.0f);
        ED.PowerCal_100W_Psat_mW[ED.currentBand[ED.activeVFO]] = f.P_sat;
        ED.PowerCal_100W_kindex[ED.currentBand[ED.activeVFO]] = f.k;
    } else {
        f = FitPowerCurve(attenuations_dB, powers_mW, Npoints, 15000.0f,6.0f);
        ED.PowerCal_20W_Psat_mW[ED.currentBand[ED.activeVFO]] = f.P_sat;
        ED.PowerCal_20W_kindex[ED.currentBand[ED.activeVFO]] = f.k;
    }
    // Reset back to the 0dB power measurement in case the user wants to do it again
    // User must press button 12 to transition to CALIBRATE_OFFSET_SPACE state
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 0.0;
    SetTXAttenuation(ED.XAttenCW[ED.currentBand[ED.activeVFO]]);
    if (ED.PA100Wactive){
        if (powerUnit)
            measuredPower = CW_100W_CAL_POWER_POINT_W;
        else
            measuredPower = 10*log10f(CW_100W_CAL_POWER_POINT_W*1000); // dBm units
    }else{
        if (powerUnit)
            measuredPower = CW_20W_CAL_POWER_POINT_W;
        else
            measuredPower = 10*log10f(CW_20W_CAL_POWER_POINT_W*1000); // dBm units
    }
    targetPower = measuredPower;
    PanePowerAtt.stale = true;
    PanePowerPower.stale = true;
    PanePowerInstructions.stale = true;
}

void ChangeCalibrationPASelection(void){
    if (ED.PA100Wactive){
        if (powerUnit)
            measuredPower = CW_20W_CAL_POWER_POINT_W;
        else
            measuredPower = 10*log10f(CW_20W_CAL_POWER_POINT_W*1000); // dBm units
        targetPower = measuredPower;
        ED.PA100Wactive = false;
    }else{
        if (powerUnit)
            measuredPower = CW_100W_CAL_POWER_POINT_W;
        else
            measuredPower = 10*log10f(CW_100W_CAL_POWER_POINT_W*1000); // dBm units
        targetPower = measuredPower;
        ED.PA100Wactive = true;
    }
    // Force hardware update even though we're staying in the same calibration state
    // This ensures the PA selection gets applied to the hardware
    ForceUpdateRFHardwareState();
    Npoints = 0;
    powerCalibrationStepCount = 0; // Reset step counter when changing PA
    PanePowerData.stale = true;
    PanePowerInstructions.stale = true;
}

void RecordPowerDataPoint(void){
    switch (modeSM.state_id){
        case ModeSm_StateId_CALIBRATE_POWER_SPACE:{
            if (Npoints >= sizeof(powers_W)/sizeof(powers_W[0]))
                Npoints = 0;
            attenuations_dB[Npoints] = ED.XAttenCW[ED.currentBand[ED.activeVFO]];
            if (powerUnit)
                powers_W[Npoints] = measuredPower;
            else
                powers_W[Npoints] = pow(10.0f,measuredPower/10.0f)/1000.0f;
            Npoints++;

            // Update step counter, but cap at 3 (don't go to step 4 until in OFFSET_SPACE)
            // This allows re-measurement without incorrectly showing step 4 as complete
            if (powerCalibrationStepCount < 3) {
                powerCalibrationStepCount++;
            }

            PanePowerInstructions.stale = true; // Update instructions display
            // Change the target power by factor of 6 dB
            if (Npoints < 3){
                if (powerUnit)
                    targetPower = targetPower / 4.0;
                else
                    targetPower = targetPower - 6.0;
                measuredPower = targetPower;
            }else{
                // We have recorded all three points, calculate the power curve
                // But do NOT automatically transition state - user must press button 12
                CalculatePowerCurveFit();
            }
            // Note: User can re-measure points or press button 12 to advance
            PanePowerData.stale = true;
            break;
        }
        case ModeSm_StateId_CALIBRATE_OFFSET_SPACE:{
            // The measuredPower is what we're actually reading
            float32_t factor;
            float32_t powr_W = measuredPower;
            if (powerUnit == 0){
                powr_W = pow(10.0f,measuredPower/10.0f)/1000.0f;
            }
            if (ED.PA100Wactive){
                factor = SSB_100W_CAL_POWER_POINT_W / powr_W;
            } else {
                factor = SSB_20W_CAL_POWER_POINT_W / powr_W;
            }

            Debug("Factor = " + String(factor));
            float32_t corr = 10*log10f(factor);
            Debug("Therefore gain correction is [dB] = " + String(corr));
            if (ED.PA100Wactive)
                ED.PowerCal_100W_DSP_Gain_correction_dB[ED.currentBand[ED.activeVFO]] = corr;
            else
                ED.PowerCal_20W_DSP_Gain_correction_dB[ED.currentBand[ED.activeVFO]] = corr;
            // Set step counter to 4 when offset measurement is recorded
            powerCalibrationStepCount = 4;
            PanePowerInstructions.stale = true; // Update instructions display
            PanePowerData.stale = true;
            // User must press button to change measurement mode
            break;
        }
        default:
            break;
    }
}

float32_t GetPowDataSum(void){
    float32_t powsum = 0;
    for (size_t k=0; k<Npoints; k++){
        powsum += abs(powers_W[k]);
    }
    return powsum;
}

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

    tft.drawFastHLine(PanePowerData.x0+30,PanePowerData.y0+70,180,RA8875_WHITE);
    for (size_t k=0; k<=4; k++){
        int16_t y = PanePowerData.y0 + 20 + k*17;

        tft.setCursor(PanePowerData.x0+col1x, y);
        if (k < powerCalibrationStepCount){
            // Draw a check mark
            tft.setTextColor(RA8875_GREEN);
            tft.print("v"); // Checkmark character
            tft.setTextColor(RA8875_WHITE);
        }
        if ((k == powerCalibrationStepCount) && (k < 3)){
            // Draw an arrow indicating that you're on this step
            tft.print(">"); // right arrow character
        }
        // Only draw the arrow here if we transition to OFFSET mode
        if ((modeSM.state_id == ModeSm_StateId_CALIBRATE_OFFSET_SPACE) && (k == 3)){
            tft.print(">"); // right arrow character
        }
        
        if ((k == 4) && (k == powerCalibrationStepCount)){
            y = PanePowerData.y0 + 20 + (k-1)*17;
            // completed all four steps
            tft.setTextColor(RA8875_GREEN);
            tft.print("v"); // Checkmark character
            tft.setTextColor(RA8875_WHITE);
        }

        if (k < 4){
            tft.setCursor(PanePowerData.x0+col2x, y);
            tft.print((int32_t)k+1);
        }

        if ((k < Npoints) || (modeSM.state_id == ModeSm_StateId_CALIBRATE_OFFSET_SPACE)){
            // Draw attenuation and power for points 1 to 3
            tft.setCursor(PanePowerData.x0+col3x, y);
            tft.print(attenuations_dB[k]);

            tft.setCursor(PanePowerData.x0+col4x, y);
            sprintf(buff,"%3.2f",powers_W[k]);
            tft.print(buff);
        }
        if ((k == 4) && (k == powerCalibrationStepCount)){
            tft.setCursor(PanePowerData.x0+col3x, y);
            tft.print(0);

            tft.setCursor(PanePowerData.x0+col4x, y);
            if (powerUnit)
                sprintf(buff,"%3.2f",measuredPower);
            else
                sprintf(buff,"%3.2f",pow(10.0f,measuredPower/10.0f)/1000.0f);
            tft.print(buff);
        }

    }
    PanePowerData.stale = false;

}

uint8_t incindexPower = 1;
const float32_t powerincvals[] = {1, 0.1, 0.01};

void ChangePowerUnits(void){
    if (powerUnit){
        powerUnit = 0;
        targetPower = 10*log10f(targetPower*1000.0f);
        measuredPower = 10*log10f(measuredPower*1000.0f);
    } else {
        powerUnit = 1;
        targetPower = pow(10.0f,targetPower/10.0f)/1000.0f;
        measuredPower = pow(10.0f,measuredPower/10.0f)/1000.0f;
    }
    PanePowerPower.stale = true;
}

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
    measuredPower += powerincvals[incindexPower];
    if (measuredPower > 100.0)
        measuredPower = 100.0;
}

void DecrementCalibrationPower(void){
    measuredPower -= powerincvals[incindexPower];
    if (measuredPower < 0.0)
        measuredPower = 0.0;
}

/**
 * @brief Increase transmit attenuation during power calibration
 * @note Increments by 0.5 dB, clamped to maximum 31.5 dB
 * @note Modifies ED.XAttenCW for current band
 * @note Called when user rotates volume encoder clockwise in power cal mode
 */
void IncrementCalibrationTransmitAtt(void){
    int32_t currentBand = ED.currentBand[ED.activeVFO];
    ED.XAttenCW[currentBand] += 0.5;
    if (ED.XAttenCW[currentBand] > 31.5)
        ED.XAttenCW[currentBand] = 31.5;
    PanePowerAtt.stale = true;
    SetTXAttenuation(ED.XAttenCW[currentBand]);
}

/**
 * @brief Decrease transmit attenuation during power calibration
 * @note Decrements by 0.5 dB, clamped to minimum 0.0 dB
 * @note Modifies ED.XAttenCW for current band
 * @note Called when user rotates volume encoder counter-clockwise in power cal mode
 */
void DecrementCalibrationTransmitAtt(void){
    int32_t currentBand = ED.currentBand[ED.activeVFO];
    ED.XAttenCW[currentBand] -= 0.5;
    if (ED.XAttenCW[currentBand] < 0.0)
        ED.XAttenCW[currentBand] = 0.0;
    PanePowerAtt.stale = true;
    SetTXAttenuation(ED.XAttenCW[currentBand]);
}

float32_t oldpow = -5.0;
float32_t oldtargetPower = 0.0;
/**
 * @brief Render the power display pane
 * @note Shows measured power during calibration
 */
static void DrawPowerPowerPane(void){
    if ((oldpow != measuredPower) || (oldtargetPower != targetPower) ) 
        PanePowerPower.stale = true;
    oldpow = measuredPower;
    oldtargetPower = targetPower;
    if (!PanePowerPower.stale) return;
    
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);

    tft.fillRect(PanePowerPower.x0-tft.getFontWidth()*7, PanePowerPower.y0, PanePowerPower.width+tft.getFontWidth()*7, PanePowerPower.height, RA8875_BLACK);

    tft.setCursor(PanePowerPower.x0,PanePowerPower.y0);
    if (powerUnit)
        sprintf(buff,"%3.2fW ",measuredPower);
    else
        sprintf(buff,"%2.1fdBm ",measuredPower);
    tft.print(buff);
    tft.setTextColor(RA8875_MAGENTA);
    sprintf(buff,"%3.2f",targetPower);
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
        (oldpu != powerUnit))
        PanePowerAdjust.stale = true;
    oldPincind = incindexPower;
    oldPband = ED.currentBand[ED.activeVFO];
    oldpasel = ED.PA100Wactive;
    oldmode = modeSM.state_id;
    oldpu = powerUnit;

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
    if (powerUnit)
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
    // Use powerCalibrationStepCount to track all steps 1-4
    if (powerCalibrationStepCount >= 1){
        tft.setTextColor(RA8875_GREEN);
        tft.print("\x76"); // Checkmark character
        tft.setTextColor(RA8875_WHITE);
        tft.print("-Record power level at 0dB atten");
    } else {
        tft.print("1-Record power level at 0dB atten");
    }

    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    if (powerCalibrationStepCount >= 2){
        tft.setTextColor(RA8875_GREEN);
        tft.print("\x76"); // Checkmark character
        tft.setTextColor(RA8875_WHITE);
        tft.print("-Adjust atten to drop pow by 6dB");
    } else {
        tft.print("2-Adjust atten to drop pow by 6dB");
    }

    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    if (powerCalibrationStepCount >= 3){
        tft.setTextColor(RA8875_GREEN);
        tft.print("\x76"); // Checkmark character
        tft.setTextColor(RA8875_WHITE);
        tft.print("-Adjust atten to drop power by");
    } else {
        tft.print("3-Adjust atten to drop power by");
    }
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("  a further 6dB");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    // Step 4 is only checked when powerCalibrationStepCount reaches 4 (in CALIBRATE_OFFSET_SPACE)
    if (powerCalibrationStepCount >= 4){
        tft.setTextColor(RA8875_GREEN);
        tft.print("\x76"); // Checkmark character
        tft.setTextColor(RA8875_WHITE);
        tft.print("-Record measured power");
    } else {
        tft.print("4-Record measured power");
    }
    
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
    tft.print("proceed to step 4, or remeasure");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    //Limits:("                                 ");
    tft.print("data points in steps 1 through 3.");

    
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

void ResetPowerCal(void){
    switch (modeSM.state_id){
        case (ModeSm_StateId_CALIBRATE_POWER_SPACE):
            // Reset step counter and data points when in POWER_SPACE
            // This happens when returning from OFFSET_SPACE or changing bands
            powerCalibrationStepCount = 0;
            Npoints = 0;

            if (ED.PA100Wactive){
                if (powerUnit)
                    measuredPower = CW_100W_CAL_POWER_POINT_W;
                else
                    measuredPower = 10*log10f(CW_100W_CAL_POWER_POINT_W*1000); // dBm units
            }else{
                if (powerUnit)
                    measuredPower = CW_20W_CAL_POWER_POINT_W;
                else
                    measuredPower = 10*log10f(CW_20W_CAL_POWER_POINT_W*1000); // dBm units
            }
            break;
        case (ModeSm_StateId_CALIBRATE_OFFSET_SPACE):
            // When entering OFFSET_SPACE from POWER_SPACE, counter is 3 - preserve it
            // When changing bands while in OFFSET_SPACE, counter is 4 - reset it
            if (powerCalibrationStepCount >= 4) {
                // Band change case: we completed step 4, reset for new band
                powerCalibrationStepCount = 0;
            }
            // Otherwise preserve counter at 3 when entering OFFSET_SPACE
            Npoints = 0;

            if (ED.PA100Wactive){
                if (powerUnit)
                    measuredPower = SSB_100W_CAL_POWER_POINT_W;
                else
                    measuredPower = 10*log10f(SSB_100W_CAL_POWER_POINT_W*1000); // dBm units
                targetPower = measuredPower;
                ED.PowerCal_100W_DSP_Gain_correction_dB[ED.currentBand[ED.activeVFO]] = -3.0;
            } else {
                if (powerUnit)
                    measuredPower = SSB_20W_CAL_POWER_POINT_W;
                else
                    measuredPower = 10*log10f(SSB_20W_CAL_POWER_POINT_W*1000); // dBm units
                targetPower = measuredPower;
                ED.PowerCal_20W_DSP_Gain_correction_dB[ED.currentBand[ED.activeVFO]] = -3.0;
            }
            break;
        default:
            break;
    }
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 0.0;
    targetPower = measuredPower;
    // Mark all the panes stale to force a screen refresh
    for (size_t i = 0; i < NUMBER_OF_POWER_PANES; i++){
        PowerWindowPanes[i]->stale = true;
    }
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

        if (ED.PA100Wactive == 0){
            if (powerUnit)
                measuredPower = CW_20W_CAL_POWER_POINT_W;
            else
                measuredPower = 10*log10(CW_20W_CAL_POWER_POINT_W*1000.0);
            targetPower = measuredPower;
        } else {
            if (powerUnit)
                measuredPower = CW_100W_CAL_POWER_POINT_W;
            else
                measuredPower = 10*log10(CW_100W_CAL_POWER_POINT_W*1000.0);
            targetPower = measuredPower;
        }
        // Reset calibration step counter on entry
        powerCalibrationStepCount = 0;
        Npoints = 0;
        
        // Make all the attenuations zero so we start off right
        for (size_t k = FIRST_BAND; k<=LAST_BAND; k++)
            ED.XAttenCW[k] = 0.0;
        SetTXAttenuation(ED.XAttenCW[ED.currentBand[ED.activeVFO]]);

        // Mark all the panes stale to force a screen refresh
        for (size_t i = 0; i < NUMBER_OF_POWER_PANES; i++){
            PowerWindowPanes[i]->stale = true;
        }

    }

    for (size_t i = 0; i < NUMBER_OF_POWER_PANES; i++){
        PowerWindowPanes[i]->DrawFunction();
    }
}
