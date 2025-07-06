#ifndef FRONTPANEL_H
#define FRONTPANEL_H
#include "SDT.h"

#define V12_PANEL_MCP23017_ADDR_1 0x20
#define V12_PANEL_MCP23017_ADDR_2 0x21
#define LED_1_PORT 6
#define LED_2_PORT 7
#define INT_PIN_1 14
#define INT_PIN_2 15

void FrontPanelInit(void);
void CheckForFrontPanelInterrupts(void);
int32_t GetButton(void);

#endif // FRONTPANEL_H

