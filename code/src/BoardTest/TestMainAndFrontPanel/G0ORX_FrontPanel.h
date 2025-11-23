#ifndef G0ORX_FRONTPANEL_H
#define G0ORX_FRONTPANEL_H

#include <stdint.h>
#include "G0ORX_Rotary.h"

extern G0ORX_Rotary volumeEncoder;
extern G0ORX_Rotary filterEncoder;
extern G0ORX_Rotary tuneEncoder;
extern G0ORX_Rotary fineTuneEncoder;

extern int G0ORXButtonPressed;
extern int G0ORXSwitchPressed;


extern void FrontPanelInit();
extern void FrontPanelSetLed(int led, uint8_t state);
int FrontPanelCheck(uint8_t* encoder_num);
#endif