#ifndef LOOP_H
#define LOOP_H

// Define the types of interrupt events we will experience
typedef enum {
    iNONE,
    iPTT_PRESSED,
    iPTT_RELEASED,
    iMODE,
    iCALIBRATE_CW_PA,
    iCALIBRATE_EXIT,
    iCALIBRATE_FREQUENCY,
    iCALIBRATE_RX_IQ,
    iCALIBRATE_SSB_PA,
    iCALIBRATE_TX_IQ,
    iKEY1_PRESSED,
    iKEY1_RELEASED,
    iKEY2_PRESSED
} InterruptType;

void FrontPanelInterrupt(void);
void ConsumeInterrupt(void);
InterruptType GetInterrupt(void);
void SetInterrupt(InterruptType i);
void loop(void);
void SetKeyType(KeyTypeId key);
void SetKey1Dit(void);
void SetKey1Dah(void);


#endif // LOOP_H
