#include "../src/PhoenixSketch/SDT.h"

static int32_t ButtonPressed = -1;

void CheckForFrontPanelInterrupts(void){}

int32_t GetButton(void){
    return ButtonPressed;
}

void SetButton(int32_t bt){
    ButtonPressed = bt;
}
