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
            extern size_t primaryMenuIndex;
            extern size_t secondaryMenuIndex;
            extern struct PrimaryMenuOption primaryMenu[6];

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
        default:
            break;
    }
    oldstate = uiSM.state_id;
}
