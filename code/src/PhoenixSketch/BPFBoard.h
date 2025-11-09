#ifndef BPF_CONTROL_H
#define BPF_CONTROL_H

// BPF control macros
#define SET_BPF_BAND(val) (hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)val & 0x0000000F) << BPFBAND0BIT));buffer_add()
#define GET_BPF_BAND ((hardwareRegister & 0xF0000000) >> BPFBAND0BIT)

/*
BPF word is the control word we need to write to the BPF GPIOAB register. How do we
calculate the BPF word, given the band number?
 BPF band        BPF word   Band code Band# 1<<#                 Hex     Flip hex bytes
 BPF_BAND_BYPASS 0x0008     0b1111    15    1000 0000 0000 0000  0x8000  0x0080
 BPF_BAND_6M     0x0004     0b1010    10    0000 0100 0000 0000  0x0400  0x0004
 BPF_BAND_10M    0x0002     0b1001    9     0000 0010 0000 0000  0x0200  0x0002
 BPF_BAND_12M    0x0001     0b1000    8     0000 0001 0000 0000  0x0100  0x0001
 BPF_BAND_15M    0x8000     0b0111    7     0000 0000 1000 0000  0x0080  0x8000
 BPF_BAND_17M    0x4000     0b0110    6     0000 0000 0100 0000  0x0040  0x4000
 BPF_BAND_20M    0x2000     0b0101    5     0000 0000 0010 0000  0x0020  0x2000
 BPF_BAND_30M    0x1000     0b0100    4     0000 0000 0001 0000  0x0010  0x1000
 BPF_BAND_40M    0x0800     0b0011    3     0000 0000 0000 1000  0x0008  0x0800
 BPF_BAND_60M    0x0100     0b0000    0     0000 0000 0000 0001  0x0001  0x0100
 BPF_BAND_80M    0x0400     0b0010    2     0000 0000 0000 0100  0x0004  0x0400
 BPF_BAND_160M   0x0200     0b0001    1     0000 0000 0000 0010  0x0002  0x0200

Therefore, to get which bit to make high in the control word (i.e., get the BPF word):
1) Calculate (1 << band#) 
2) Flip bytes
3) Handle special case of bypass
*/
#define BPF_WORD ({ \
    uint16_t shifted = 1 << GET_BPF_BAND; \
    uint16_t swapped = ((shifted >> 8) & 0xFF) | ((shifted & 0xFF) << 8); \
    (swapped == 0x0080) ? (swapped >> 4) : swapped; \
})

/**
 * @brief Initialize the BPF board hardware and GPIO control
 * @return ESUCCESS on success, ENOI2C if I2C communication fails
 * @note Configures MCP23017 GPIO expander for band-pass filter relay control
 */
errno_t InitializeBPFBoard(void);

/**
 * @brief Select band-pass filter for specified amateur radio band
 * @param band Band number (0-10, 15): 60M, 160M, 80M, 40M, 30M, 20M, 17M, 15M, 12M, 10M, 6M, or 15=BYPASS
 * @note Switches appropriate BPF relay for optimal filtering on selected band
 * @note Band selection uses byte-swapped encoding per BPF_WORD macro
 */
void SelectBPFBand(int32_t band);

/**
 * @brief Get the current MCP23017 GPIO register state for testing
 * @return 16-bit register value combining GPIOA and GPIOB
 * @note This function is intended for unit testing only
 */
uint16_t GetBPFMCPRegisters(void);

#endif // BPF_CONTROL_H
