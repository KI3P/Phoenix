#include "SDT.h"

#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   480
#define NUMBER_OF_PANES 13

struct Pane {
    uint16_t x0;     /**< top left corner, horizontal coordinate */
    uint16_t y0;     /**< top left corner, vertical coordinate */
    uint16_t width;  /**< horizontal left to right size */
    uint16_t height; /**< vertical top to bottom size */
    void (*DrawFunction)(void); /**< Pointer to function that draws the pane */
    bool stale;      /**< true if the pane information needs to be updated */
};

Pane PaneActiveVFO =   {5,5,280,50,DrawActiveVFOPane,1};
Pane PaneInactiveVFO = {300,5,220,40,DrawInactiveVFOPane,1};
Pane PaneFreqBandMod = {5,60,310,30,DrawFreqBandModPane,1};
Pane PaneSpectrum =    {5,95,520,170,DrawSpectrumPane,1};
Pane PaneWaterfall =   {5,270,520,170,DrawWaterfallPane,1};
Pane PaneStateOfHealth={5,445,260,30,DrawStateOfHealthPane,1};
Pane PaneTime =        {270,445,260,30,DrawTimePane,1};
Pane PaneSWR =         {535,15,150,40,DrawSWRPane,1};
Pane PaneTXRXStatus =  {710,20,60,30,DrawTXRXStatusPane,1};
Pane PaneSMeter =      {535,60,260,50,DrawSMeterPane,1};
Pane PaneAudioSpectrum={535,115,260,150,DrawAudioSpectrumPane,1};
Pane PaneSettings =    {535,270,260,170,DrawSettingsPane,1};
Pane PaneNameBadge =   {535,445,260,30,DrawNameBadgePane,1};

// An array of all the panes, useful for iterating over all the panes
Pane WindowPanes[NUMBER_OF_PANES] = {PaneActiveVFO,PaneInactiveVFO,PaneFreqBandMod,
                                    PaneSpectrum,PaneWaterfall,PaneStateOfHealth,
                                    PaneTime,PaneSWR,PaneTXRXStatus,
                                    PaneSMeter,PaneAudioSpectrum,PaneSettings,
                                    PaneNameBadge};

void DrawActiveVFOPane(void) {}
void DrawInactiveVFOPane(void) {}
void DrawFreqBandModPane(void) {}
void DrawSpectrumPane(void) {}
void DrawWaterfallPane(void) {}
void DrawStateOfHealthPane(void) {}
void DrawTimePane(void) {}
void DrawSWRPane(void) {}
void DrawTXRXStatusPane(void) {}
void DrawSMeterPane(void) {}
void DrawAudioSpectrumPane(void) {}
void DrawSettingsPane(void) {}
void DrawNameBadgePane(void) {}


//#ifdef TEST_MODE
// TEST FUNCTIONS, USED IN GOOGLE TEST HARNESS. MAY BE UNNECESSARY
static int32_t intVal;
int32_t GetInt(void){
    return intVal;
}
errno_t SetInt(int32_t val){
    intVal = val;
    return ESUCCESS;
}
UIValueUpdateInt uiRFScaleUpdate = {
    .base = TYPE_INT32,
    .getValueFunction = GetInt,
    .incrementValue = 1,
    .setValueFunction = SetInt
};
//#endif

UIValueUpdateFloat uiRXGainUpdate = {
    .base = TYPE_FLOAT,
    .getValueFunction = GetRXAttenuation,
    .incrementValue = 0.5,
    .setValueFunction = SetRXAttenuation // TODO: implement this via an interrupt instead
};

UIValueUpdateFloat uiTXGainUpdate = {
    .base = TYPE_FLOAT,
    .getValueFunction = GetTXAttenuation,
    .incrementValue = 0.5,
    .setValueFunction = SetTXAttenuation // TODO: implement this via an interrupt instead
};

/**
 * Increment the value of a variable in the UI. Uses the UI state machine structure 
 * variable uiUp which provides the function that gets the current value, provides the
 * magnitude of the increment, and provides the function that sets the new value.
 */
void UIIncValue(void){
    Base *base = (Base *)uiSM.vars.uiUp;
    switch (base->type){
        case TYPE_INT32: {
            UIValueUpdateInt *foo = (UIValueUpdateInt *)uiSM.vars.uiUp;
            int32_t prior_value = foo->getValueFunction();
            int32_t new_value = prior_value + foo->incrementValue;
            foo->setValueFunction(new_value);
            break;
        }
        case TYPE_FLOAT: {
            UIValueUpdateFloat *foo = (UIValueUpdateFloat *)uiSM.vars.uiUp;
            float prior_value = foo->getValueFunction();
            float new_value = prior_value + foo->incrementValue;
            foo->setValueFunction(new_value);
            break;
        }
    }
}

/**
 * Decrement the value of a variable in the UI. Uses the UI state machine structure 
 * variable uiUp which provides the function that gets the current value, provides the
 * magnitude of the decrement, and provides the function that sets the new value.
 */
void UIDecValue(void){
    Base *base = (Base *)uiSM.vars.uiUp;
    switch (base->type){
        case TYPE_INT32: {
            UIValueUpdateInt *foo = (UIValueUpdateInt *)uiSM.vars.uiUp;
            int32_t prior_value = foo->getValueFunction();
            int32_t new_value = prior_value - foo->incrementValue;
            foo->setValueFunction(new_value);
            break;
        }
        case TYPE_FLOAT: {
            UIValueUpdateFloat *foo = (UIValueUpdateFloat *)uiSM.vars.uiUp;
            float prior_value = foo->getValueFunction();
            float new_value = prior_value - foo->incrementValue;
            foo->setValueFunction(new_value);
            break;
        }
    }
}
