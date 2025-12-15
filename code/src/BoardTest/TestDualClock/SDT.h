#include <stdint.h>

typedef int errno_t;

#define ESUCCESS             0
#define ENOI2C              -1
#define EGPIOWRITEFAIL      -2
#define EFAIL               -10

#define SI5351_BUS_BASE_ADDR    0x60

// Teensy pins used for particular functions
#define CW_ON_OFF   5 // CW on / off (H=ON,L=OFF) (V12 hardware)
// SDA 21
// SCL 22

#define Debug(x) Serial.println(x)

// The bit map for hardwareRegister
#define LPFBAND0BIT  0
#define LPFBAND1BIT  1
#define LPFBAND2BIT  2
#define LPFBAND3BIT  3
#define ANT0BIT      4
#define ANT1BIT      5
#define XVTRBIT      6
#define PA100WBIT    7
#define TXBPFBIT     8
#define RXBPFBIT     9
#define RXTXBIT      10
#define CWBIT        11
#define MODEBIT      12
#define CALBIT       13
#define CWVFOBIT     14
#define SSBVFOBIT    15
#define TXATTLSB     16
#define TXATTMSB     21
#define RXATTLSB     22
#define RXATTMSB     27
#define BPFBAND0BIT  28
#define BPFBAND1BIT  29
#define BPFBAND2BIT  30
#define BPFBAND3BIT  31


// Macros used to manipulate the hardware register
#define GET_BIT(byte, bit) (((byte) >> (bit)) & 1)
#define SET_BIT(byte, bit) ((byte) |= (1 << (bit)))
#define CLEAR_BIT(byte, bit) ((byte) &= ~(1 << (bit)))
#define TOGGLE_BIT(byte, bit) ((byte) ^= (1 << (bit)))
#define GET_LPF_BAND (uint8_t)(hardwareRegister & 0x0000000F)