#include "../src/PhoenixSketch/SDT.h"
#include "AudioIO_mock.h"

uint32_t CCM_CS1CDR;
uint32_t CCM_CS1CDR_SAI1_CLK_PRED_MASK;
uint32_t CCM_CS1CDR_SAI1_CLK_PODF_MASK;
uint32_t CCM_CS2CDR;
uint32_t CCM_CS2CDR_SAI2_CLK_PRED_MASK;
uint32_t CCM_CS2CDR_SAI2_CLK_PODF_MASK;
FILE* serialfile;

void AudioMemory(uint16_t mem){}
void AudioMemory_F32(uint16_t mem){}
void set_audioClock(int c0, int c1, int c2, bool b){}
uint32_t CCM_CS1CDR_SAI1_CLK_PRED(int a){return 0;}
uint32_t CCM_CS1CDR_SAI1_CLK_PODF(int a){return 0;}
uint32_t CCM_CS2CDR_SAI2_CLK_PRED(int a){return 0;}
uint32_t CCM_CS2CDR_SAI2_CLK_PODF(int a){return 0;}

void digitalWrite(uint16_t pin, uint8_t val){}
uint8_t digitalRead(uint16_t pin){return 0;}
void pinMode(uint16_t pin, uint8_t val){}

void serialprint(const char *msg){
    if (serialfile != nullptr){
        fprintf(serialfile, "%s\n", msg);
    }
}

void serialprint(char *msg){
    serialprint((const char *)msg);
}

void serialprint(float *value){
    if (serialfile != nullptr){
        fprintf(serialfile, "%11.10f\n",*value);
    }
}

void serialprintBegin(char *fname){
    if (fname != nullptr){
        serialfile = fopen(fname, "w");
    }
}

void serialprintEnd(void){
    fclose(serialfile);
}

//Serial.println(strbuf);
