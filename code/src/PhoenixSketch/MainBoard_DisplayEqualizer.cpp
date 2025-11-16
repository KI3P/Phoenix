/**
 * @file MainBoard_DisplayEqualizer.cpp
 * @brief Adjustment of the equalizer levels
 *
 * @see window_panes.drawio, EqualizerAdjust tab for layout
 * @see MainBoard_Display.cpp for core display infrastructure
 * @see MainBoard_DisplayMenus.cpp for menu system
 */

#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>
extern RA8875 tft;

static const int8_t NUMBER_OF_PANES = 4;

// Forward declaration of the pane drawing functions
static void DrawRXEqualizerPane(void);
static void DrawTXEqualizerPane(void);
static void DrawIncrementPane(void);
static void DrawInstructionsPane(void);


// Pane instances
static Pane PaneRXEqualizer =  {80,60,430,100,DrawRXEqualizerPane,1};
static Pane PaneTXEqualizer =  {80,340,430,100,DrawTXEqualizerPane,1};
static Pane PaneIncrement =    {460,230,60,40,DrawIncrementPane,1};
static Pane PaneInstructions = {537,7,260,470,DrawInstructionsPane,1};

// Array of all panes for iteration
static Pane* WindowPanes[NUMBER_OF_PANES] = {&PaneRXEqualizer,&PaneTXEqualizer,
                                    &PaneIncrement,&PaneInstructions};

/**
 * @brief Main equalizer adjustment screen rendering function
 * @note Called from DrawDisplay() when in EQUALIZER UI state
 * @note Displays receive and transmit equalizer bars with adjustment controls
 */
void DrawEqualizerAdjustment(void){
    if (!(uiSM.state_id == UISm_StateId_EQUALIZER) )
        return;
    tft.writeTo(L1);
    if (uiSM.vars.clearScreen){
        tft.fillWindow();
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        uiSM.vars.clearScreen = false;

        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)1);
        tft.setCursor(10,10);
        tft.print("Receive Equalizer");

        tft.setCursor(10,290);
        tft.print("Transmit Equalizer");

        tft.setFontScale((enum RA8875tsize)0);
        tft.setCursor(50,60);
        tft.print("100");
        tft.setCursor(50,160);
        tft.print("  0");

        tft.setCursor(50,340);
        tft.print("100");
        tft.setCursor(50,440);
        tft.print("  0");

        for (size_t i = 0; i < NUMBER_OF_PANES; i++){
            WindowPanes[i]->stale = true;
        }
    }
    for (size_t i = 0; i < NUMBER_OF_PANES; i++){
        WindowPanes[i]->DrawFunction();
    }
}

static uint8_t cellSelection = 0;
const uint8_t RECEIVE = 0;
const uint8_t TRANSMIT = 1;
static uint8_t rxtxSelection = 0;

/**
 * @brief Toggle between receive and transmit equalizer editing
 * @note Switches rxtxSelection between RECEIVE (0) and TRANSMIT (1)
 * @note Called when user presses button 15
 */
void ToggleRXTXEqualizerEdit(void){
    if (rxtxSelection == 0)
        rxtxSelection = 1;
    else
        rxtxSelection = 0;
}

/**
 * @brief Move selection to next equalizer frequency band (with wrap-around)
 * @note Cycles through EQUALIZER_CELL_COUNT frequency bands (typically 14)
 * @note Called when user rotates volume encoder clockwise
 */
void IncrementEqualizerSelection(void){
    cellSelection++;
    if (cellSelection >= EQUALIZER_CELL_COUNT)
        cellSelection = 0;
}

/**
 * @brief Move selection to previous equalizer frequency band (with wrap-around)
 * @note Cycles backward through EQUALIZER_CELL_COUNT frequency bands
 * @note Called when user rotates volume encoder counter-clockwise
 */
void DecrementEqualizerSelection(void){
    if (cellSelection >= 1)
        cellSelection--;
    else
        cellSelection = EQUALIZER_CELL_COUNT-1;
}

static const int16_t increments[] = {1,10};
static uint8_t incIndex = 0;

/**
 * @brief Cycle through available equalizer adjustment increments
 * @note Toggles between increment values defined in increments[] array (1, 10)
 * @note Called when user presses button 16
 */
void AdjustEqualizerIncrement(void){
    incIndex++;
    if (incIndex >= sizeof(increments)/sizeof(increments[0]))
        incIndex = 0;
}

/**
 * @brief Increase the gain of the currently selected equalizer frequency band
 * @note Operates on either RX or TX equalizer based on rxtxSelection
 * @note Increments by current increment value (1 or 10)
 * @note Clamps maximum value to 100
 * @note Called when user rotates filter encoder clockwise
 */
void IncrementEqualizerValue(void){
    int32_t *equalizer;
    if (rxtxSelection == RECEIVE)
        equalizer = ED.equalizerRec;
    else
        equalizer = ED.equalizerXmt;
    equalizer[cellSelection] += increments[incIndex];
    if (equalizer[cellSelection] > 100)
        equalizer[cellSelection] = 100;
}

/**
 * @brief Decrease the gain of the currently selected equalizer frequency band
 * @note Operates on either RX or TX equalizer based on rxtxSelection
 * @note Decrements by current increment value (1 or 10)
 * @note Clamps minimum value to 0
 * @note Called when user rotates filter encoder counter-clockwise
 */
void DecrementEqualizerValue(void){
    int32_t *equalizer;
    if (rxtxSelection == RECEIVE)
        equalizer = ED.equalizerRec;
    else
        equalizer = ED.equalizerXmt;
    equalizer[cellSelection] -= increments[incIndex];
    if (equalizer[cellSelection] < 0)
        equalizer[cellSelection] = 0;
}

/**
 * @brief Calculate sum of all receive equalizer band values
 * @return Sum of all EQUALIZER_CELL_COUNT receive equalizer values
 * @note Used to detect changes requiring pane redraw
 */
int32_t SumRXEq(void){
    int32_t sum = 0;
    for (uint8_t i = 0; i < EQUALIZER_CELL_COUNT; i++) {
        sum += ED.equalizerRec[i];
    }
    return sum;
}

static int32_t oldRsum = 0;
static uint8_t oldRCellSel = 25;
static uint8_t oldrsel = 5;
/**
 * @brief Render the receive equalizer bar graph pane
 * @note Displays 14 frequency band bars with current selection highlighted
 * @note Selected band shown in green, other bands in blue (when editing RX)
 * @note Dimmed/gray when transmit equalizer is being edited
 */
void DrawRXEqualizerPane(void) {
    int32_t newsum = SumRXEq();
    if ((oldRsum != newsum) || (oldRCellSel != cellSelection) || (oldrsel != rxtxSelection)){
        PaneRXEqualizer.stale = true;
    }
    oldRsum = newsum;
    oldRCellSel = cellSelection;
    oldrsel = rxtxSelection;
    if (!PaneRXEqualizer.stale) return;
    PaneRXEqualizer.stale = false;

    tft.fillRect(PaneRXEqualizer.x0, PaneRXEqualizer.y0, PaneRXEqualizer.width, PaneRXEqualizer.height, DARKGREY);

    if (rxtxSelection == RECEIVE)
        tft.drawRect(PaneRXEqualizer.x0, PaneRXEqualizer.y0, PaneRXEqualizer.width, PaneRXEqualizer.height, RA8875_GREEN);
    else
        tft.drawRect(PaneRXEqualizer.x0, PaneRXEqualizer.y0, PaneRXEqualizer.width, PaneRXEqualizer.height, DARKGREY);

    // Draw each of the equalizer bars
    uint16_t x0,y0,width,height,color;
    for (uint8_t i = 0; i < EQUALIZER_CELL_COUNT; i++) {
        color = DARKGREY; // draw them grey is we're not being edited
        if (rxtxSelection == RECEIVE){
            if (cellSelection == i)
                color = RA8875_GREEN;
            else 
                color = RA8875_BLUE;
        }
        width = 20;
        height = ED.equalizerRec[i];
        x0 = PaneRXEqualizer.x0 + 10 + 30*i;
        y0 = PaneRXEqualizer.y0 + PaneRXEqualizer.height - height;
        tft.fillRect(x0,y0,width,height,color);
    }
}

/**
 * @brief Calculate sum of all transmit equalizer band values
 * @return Sum of all EQUALIZER_CELL_COUNT transmit equalizer values
 * @note Used to detect changes requiring pane redraw
 */
int32_t SumTXEq(void){
    int32_t sum = 0;
    for (uint8_t i = 0; i < EQUALIZER_CELL_COUNT; i++) {
        sum += ED.equalizerXmt[i];
    }
    return sum;
}

static int32_t oldTsum = 0;
static uint8_t oldTCellSel = 25;
static uint8_t oldtsel = 5;
/**
 * @brief Render the transmit equalizer bar graph pane
 * @note Displays 14 frequency band bars with current selection highlighted
 * @note Selected band shown in green, other bands in blue (when editing TX)
 * @note Dimmed/gray when receive equalizer is being edited
 */
void DrawTXEqualizerPane(void) {
    int32_t newsum = SumTXEq();
    if ((oldTsum != newsum) || (oldTCellSel != cellSelection) || (oldtsel != rxtxSelection)){
        PaneTXEqualizer.stale = true;
    }
    oldTsum = newsum;
    oldTCellSel = cellSelection;
    oldtsel = rxtxSelection;
    if (!PaneTXEqualizer.stale) return;
    PaneTXEqualizer.stale = false;

    tft.fillRect(PaneTXEqualizer.x0, PaneTXEqualizer.y0, PaneTXEqualizer.width, PaneTXEqualizer.height, DARKGREY);

    if (rxtxSelection == TRANSMIT)
        tft.drawRect(PaneTXEqualizer.x0, PaneTXEqualizer.y0, PaneTXEqualizer.width, PaneTXEqualizer.height, RA8875_GREEN);
    else
        tft.drawRect(PaneTXEqualizer.x0, PaneTXEqualizer.y0, PaneTXEqualizer.width, PaneTXEqualizer.height, DARKGREY);

    // Draw each of the equalizer bars
    uint16_t x0,y0,width,height,color;
    for (uint8_t i = 0; i < EQUALIZER_CELL_COUNT; i++) {
        color = DARKGREY; // draw them grey is we're not being edited
        if (rxtxSelection == TRANSMIT){
            if (cellSelection == i)
                color = RA8875_GREEN;
            else 
                color = RA8875_BLUE;
        }
        width = 20;
        height = ED.equalizerXmt[i];
        x0 = PaneTXEqualizer.x0 + 10 + 30*i;
        y0 = PaneTXEqualizer.y0 + PaneTXEqualizer.height - height;
        tft.fillRect(x0,y0,width,height,color);
    }
}
    
/**
 * @brief Render the equalizer instructions pane
 * @note Displays user instructions for operating the equalizer adjustment screen
 * @note Explains button functions and encoder controls
 */
void DrawInstructionsPane(void) {
    if (!PaneInstructions.stale) return;
    PaneInstructions.stale = false;
    tft.fillRect(PaneInstructions.x0, PaneInstructions.y0, PaneInstructions.width, PaneInstructions.height, RA8875_BLACK);
    tft.drawRect(PaneInstructions.x0, PaneInstructions.y0, PaneInstructions.width, PaneInstructions.height, RA8875_YELLOW);
    int16_t x0 = PaneInstructions.x0;
    int16_t y0 = PaneInstructions.y0;    
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
    tft.print("* Press button 15 to alternate");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    between RX & TX equalizers.");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("* Press button 16 to change the");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    increment value.");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("* Use volume encoder to select");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    which cell to edit.");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("* Use filter encoder to change");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print("    cell value.");
    delta += lineD;
    tft.setCursor(x0, y0+delta);
    tft.print(" * Press Home to save and exit.");
}

static uint8_t oldindex = 5;
/**
 * @brief Render the current increment value display pane
 * @note Shows current adjustment increment (1 or 10)
 * @note Updates when user cycles increments with button 16
 */
void DrawIncrementPane(void) {
    if (oldindex != incIndex)
        PaneIncrement.stale = true;
    oldindex = incIndex;
    if (!PaneIncrement.stale) return;
    PaneIncrement.stale = false;

    tft.fillRect(PaneIncrement.x0-tft.getFontWidth()*7, PaneIncrement.y0, PaneIncrement.width+tft.getFontWidth()*7, PaneIncrement.height, RA8875_BLACK);

    tft.setTextColor(RA8875_WHITE);
    tft.setFontScale((enum RA8875tsize)1);
    tft.setCursor(PaneIncrement.x0, PaneIncrement.y0);
    tft.print(increments[incIndex]);
    tft.setCursor(PaneIncrement.x0-tft.getFontWidth()*7, PaneIncrement.y0);
    tft.print("Incr.:");

}
