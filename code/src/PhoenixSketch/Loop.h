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
    iKEY2_PRESSED,
    iVOLUME_INCREASE,
    iVOLUME_DECREASE,
    iFILTER_INCREASE,
    iFILTER_DECREASE,
    iCENTERTUNE_INCREASE,
    iCENTERTUNE_DECREASE,
    iFINETUNE_INCREASE,
    iFINETUNE_DECREASE,
    iBUTTON_PRESSED,
    iVFO_CHANGE,
    iUPDATE_TUNE,
    iMODE_CHANGE,
    iPOWER_CHANGE
} InterruptType;

void ConsumeInterrupt(void);
InterruptType GetInterrupt(void);
void SetInterrupt(InterruptType i);
void PrependInterrupt(InterruptType i);
void loop(void);
void SetKeyType(KeyTypeId key);
void SetKey1Dit(void);
void SetKey1Dah(void);
void ShutdownTeensy(void);
size_t GetInterruptFifoSize(void);
void SetupCWKeyInterrupts(void);

#endif // LOOP_H
