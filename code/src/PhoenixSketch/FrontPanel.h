#ifndef FRONTPANEL_H
#define FRONTPANEL_H
#include "SDT.h"

void InitializeFrontPanel(void);
void CheckForFrontPanelInterrupts(void);
int32_t GetButton(void);
void SetButton(int32_t);
void FrontPanelSetLed(uint8_t led, uint8_t state);

#endif // FRONTPANEL_H

