/**
 * @file MainBoard_Display.cpp
 * @brief Core display infrastructure for Phoenix SDR
 *
 * This module provides the foundational display infrastructure:
 * - TFT display object and hardware initialization
 * - Display state routing (HOME, SPLASH, MENU screens)
 *
 * Display rendering is organized into specialized modules:
 * - MainBoard_DisplayHome.cpp: Home screen panes, structures, and helper functions
 * - MainBoard_DisplayMenus.cpp: Menu system and navigation
 *
 * @see MainBoard_DisplayHome.cpp for pane definitions and rendering functions
 * @see MainBoard_DisplayMenus.cpp for menu system
 * @see RA8875 library documentation for low-level display control
 */

#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>
#include "FreeSansBold24pt7b.h"
#include "FreeSansBold18pt7b.h"

// TFT display hardware configuration
#define TFT_CS 10
#define TFT_RESET 9
RA8875 tft = RA8875(TFT_CS, TFT_RESET);

// External references to panes, variables, and functions defined in MainBoard_DisplayHome.cpp
extern bool redrawParameter;

///////////////////////////////////////////////////////////////////////////////
// HELPER FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

/**
 * Calculate the bounding rectangle for a text string.
 */
void CalculateTextCorners(int16_t x0,int16_t y0,Rectangle *rect,int16_t Nchars,
                        uint16_t charWidth,uint16_t charHeight){
    rect->x0 = x0;
    rect->y0 = y0;
    rect->width = Nchars*charWidth;
    rect->height = charHeight;
}

/**
 * Fill a rectangular area with black pixels.
 */
void BlankBox(Rectangle *rect){
    tft.fillRect(rect->x0, rect->y0, rect->width, rect->height, RA8875_BLACK);
}

/**
 * @brief Main BIT screen rendering function
 * @note Called from DrawDisplay() when in BIT UI state
 * @note Displays the results of the BIT
 */
void DrawBIT(void){
    if (!(uiSM.state_id == UISm_StateId_BIT) )
        return;
    tft.writeTo(L1);
    if (uiSM.vars.clearScreen){
        tft.fillWindow();
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        uiSM.vars.clearScreen = false;

        // Display the BIT results

        tft.setFont(&FreeSansBold24pt7b);
        tft.setTextColor(DARKGREY);
        tft.setCursor(WINDOW_WIDTH / 3 - 100, WINDOW_HEIGHT / 10);
        tft.print("I2C Status Report");

        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)1);
        char tmpbuf[80];
        uint32_t yoff = WINDOW_HEIGHT/10;
        static const int16_t col1 = 500;
        static const int16_t col2 = 700;
        tft.setCursor(5 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
        if (!bit_results.FRONT_PANEL_I2C_present) {
            tft.setTextColor(RA8875_RED);
            sprintf(tmpbuf, "Front panel MCP23017 I2C not found at 0x%02X & 0x%02X", V12_PANEL_MCP23017_ADDR_1, V12_PANEL_MCP23017_ADDR_2);
            tft.print(tmpbuf);
        } else {
            tft.setTextColor(RA8875_GREEN);
            tft.setCursor(3 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
            tft.print("Front panel MCP23017 I2C:");
            sprintf(tmpbuf, "0x%02X & 0x%02X", V12_PANEL_MCP23017_ADDR_1, V12_PANEL_MCP23017_ADDR_2);
            tft.setCursor(col1, WINDOW_HEIGHT/10 + yoff);
            tft.print(tmpbuf);
            tft.setCursor(col2, WINDOW_HEIGHT/10 + yoff);
            tft.print("PASS");
        }
        yoff += 30;

        tft.setCursor(5 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
        if (!bit_results.BPF_I2C_present) {
            tft.setTextColor(RA8875_RED);
            sprintf(tmpbuf, "BPF MCP23017 I2C not found at 0x%02X", BPF_MCP23017_ADDR);
            tft.print(tmpbuf);
        } else {
            tft.setTextColor(RA8875_GREEN);
            tft.setCursor(7 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
            tft.print("BPF MCP23017 I2C:");
            sprintf(tmpbuf, "0x%02X", BPF_MCP23017_ADDR);
            tft.setCursor(col1, WINDOW_HEIGHT/10 + yoff);
            tft.print(tmpbuf);
            tft.setCursor(col2, WINDOW_HEIGHT/10 + yoff);
            tft.print("PASS");
        }
        yoff += 30;

        tft.setCursor(5 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
        if (!bit_results.RF_I2C_present) {
            tft.setTextColor(RA8875_RED);
            sprintf(tmpbuf, "RF MCP23017 I2C not found at 0x%02X", RF_MCP23017_ADDR);
            tft.print(tmpbuf);
        } else {
            tft.setTextColor(RA8875_GREEN);
            tft.setCursor(8 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
            sprintf(tmpbuf, "0x%02X", RF_MCP23017_ADDR);
            tft.print("RF MCP23017 I2C:");
            tft.setCursor(col1, WINDOW_HEIGHT/10 + yoff);
            tft.print(tmpbuf);
            tft.setCursor(col2, WINDOW_HEIGHT/10 + yoff);
            tft.print("PASS");
        }
        yoff += 30;

        tft.setCursor(5 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
        if (!bit_results.RF_Si5351_present) {
            tft.setTextColor(RA8875_RED);
            sprintf(tmpbuf, "RF SI5351 I2C not found at 0x%02X or 0x%02X", SI5351_BUS_BASE_ADDR, SI5351_DUAL_VFO_ADDR);
            tft.print(tmpbuf);
        } else {
            tft.setTextColor(RA8875_GREEN);
            tft.setCursor(10 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
            if (HasDualVFOs())
                sprintf(tmpbuf, "0x%02X", SI5351_DUAL_VFO_ADDR);
            else
                sprintf(tmpbuf, "0x%02X", SI5351_BUS_BASE_ADDR);
            tft.print("RF SI5351 I2C:");
            tft.setCursor(col1, WINDOW_HEIGHT/10 + yoff);
            tft.print(tmpbuf);
            tft.setCursor(col2, WINDOW_HEIGHT/10 + yoff);
            tft.print("PASS");
        }
        yoff += 30;

        tft.setCursor(5 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
        if (!bit_results.V12_LPF_I2C_present) {
            tft.setTextColor(RA8875_RED);
            sprintf(tmpbuf, "K9HZ LPF MCP23017 I2C not found at 0x%02X", LPF_MCP23017_ADDR);
            tft.print(tmpbuf);
        } else {
            tft.setTextColor(RA8875_GREEN);
            tft.setCursor(4 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
            tft.print("K9HZ LPF MCP23017 I2C:");
            sprintf(tmpbuf, "0x%02X", LPF_MCP23017_ADDR);
            tft.setCursor(col1, WINDOW_HEIGHT/10 + yoff);
            tft.print(tmpbuf);
            tft.setCursor(col2, WINDOW_HEIGHT/10 + yoff);
            tft.print("PASS");
        }
        yoff += 30;

        #ifdef V12_LPF_SWR_AD7991
        tft.setCursor(5 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
        if (!bit_results.V12_LPF_AD7991_present) {
            tft.setTextColor(RA8875_RED);
            sprintf(tmpbuf, "K9HZ LPF AD7991 I2C not found at 0x%02X or 0x%02X", AD7991_I2C_ADDR1, AD7991_I2C_ADDR2);
            tft.print(tmpbuf);
        } else {
            tft.setTextColor(RA8875_GREEN);
            sprintf(tmpbuf, "0x%02X", bit_results.AD7991_I2C_ADDR);
            tft.setCursor(6 * tft.getFontWidth(), WINDOW_HEIGHT/10 + yoff);
            tft.print("K9HZ LPF AD7991 I2C:");
            tft.setCursor(col1, WINDOW_HEIGHT/10 + yoff);
            tft.print(tmpbuf);
            tft.setCursor(col2, WINDOW_HEIGHT/10 + yoff);
            tft.print("PASS");
        }
        yoff += 30;
        #endif  //V12_LPF_SWR_AD7991
    }
}


///////////////////////////////////////////////////////////////////////////////
// DISPLAY INITIALIZATION AND ROUTING
///////////////////////////////////////////////////////////////////////////////

/**
 * Initialize the RA8875 TFT display hardware and configure layers.
 */
void InitializeDisplay(void){
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    tft.begin(RA8875_800x480, 8, 20000000UL, 4000000UL);
    tft.setRotation(0);
    tft.useLayers(true);
    tft.layerEffect(OR);
    tft.clearMemory();
    tft.writeTo(L2);
    tft.clearMemory();
    tft.writeTo(L1);
    DrawDisplay();
}

UISm_StateId oldstate = UISm_StateId_ROOT;

/**
 * Main display rendering function - routes to appropriate screen based on UI state.
 *
 * Dispatches to specialized rendering functions in other modules:
 * - DrawSplash() in MainBoard_DisplayHome.cpp
 * - DrawHome() in MainBoard_DisplayHome.cpp
 * - DrawMainMenu() in MainBoard_DisplayMenus.cpp
 * - DrawSecondaryMenu() in MainBoard_DisplayMenus.cpp
 * - DrawParameter() in MainBoard_DisplayHome.cpp
 */
void DrawDisplay(void){
    switch (uiSM.state_id){
        case (UISm_StateId_SPLASH):{
            DrawSplash();
            break;
        }
        case (UISm_StateId_HOME):{
            DrawHome();
            break;
        }
        case (UISm_StateId_MAIN_MENU):{
            DrawMainMenu();
            break;
        }
        case (UISm_StateId_SECONDARY_MENU):{
            DrawSecondaryMenu();
            break;
        }
        case (UISm_StateId_UPDATE):{
            if (primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].action == variableOption){
                if (uiSM.vars.clearScreen)
                    redrawParameter = true;
                DrawHome();
                DrawParameter();
            } else {
                UISm_dispatch_event(&uiSM,UISm_EventId_HOME);
                void (*funcPtr)(void) = (void (*)(void))primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].func;
                if (funcPtr != NULL) {
                    funcPtr();
                }
            }
            break;
        }
        case (UISm_StateId_EQUALIZER):{
            DrawEqualizerAdjustment();
            break;
        }
        case (UISm_StateId_FREQ_ENTRY):{
            DrawFrequencyEntryPad();
            break;
        }
        case (UISm_StateId_BIT):{
            DrawBIT();
            break;
        }
        case (UISm_StateId_CALIBRATE_FREQUENCY):{
            DrawCalibrateFrequency();
            break;
        }
        case (UISm_StateId_CALIBRATE_RX_IQ):{
            DrawCalibrateRXIQ();
            break;
        }
        case (UISm_StateId_CALIBRATE_TX_IQ):{
            DrawCalibrateTXIQ();
            break;
        }
        case (UISm_StateId_CALIBRATE_POWER):{
            DrawCalibratePower();
            break;
        }
        default:
            break;
    }
    oldstate = uiSM.state_id;
}
