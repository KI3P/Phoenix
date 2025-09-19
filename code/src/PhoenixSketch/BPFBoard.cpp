#include "SDT.h"

static Adafruit_MCP23X17 mcpBPF; // connected to Wire2
static uint16_t BPF_GPAB_state;

errno_t InitializeBPFBoard(void){
    /**********************************************************************
     * Set up the BPF which is connected via the BANDS connector on Wire2 *
     **********************************************************************/
    SET_BPF_BAND(BandToBCD(ED.currentBand[ED.activeVFO]));   
    if (mcpBPF.begin_I2C(BPF_MCP23017_ADDR,&Wire2)){
        bit_results.BPF_I2C_present = true;
        Debug("Initialising BPF board");
        mcpBPF.enableAddrPins();
        // Set all pins to be outputs
        for (int i=0;i<16;i++){
            mcpBPF.pinMode(i, OUTPUT);
        }
        BPF_GPAB_state = BPF_WORD;
        mcpBPF.writeGPIOAB(BPF_GPAB_state);
        return ESUCCESS;
    } else {
        bit_results.BPF_I2C_present = false;
        Debug("BPF MCP23017 not found at 0x"+String(BPF_MCP23017_ADDR,HEX));
        return ENOI2C;
    }
}

void printBPFState(){
    Debug("BPF GPAB state: "+String(BPF_GPAB_state,BIN));
}

void SelectBPFBand(int32_t band) {
    if (band == -1){
        // We are in the case where the selected frequency is outside a ham band.
        // in this case, don't use a band pass
        band = LAST_BAND + 10;
    }
    // this updates the hardware register. To read the hardware register and turn it
    // into the control word, use the BPF_WORD macro
    SET_BPF_BAND(BandToBCD(band)); 
    if (BPF_GPAB_state != BPF_WORD){
        // Only write I2C traffic if the band has changed from its previous state
        mcpBPF.writeGPIOAB(BPF_GPAB_state);
        Debug("Set BPF state: "+String(BPF_GPAB_state,HEX));
        BPF_GPAB_state = BPF_WORD;
    }
}
