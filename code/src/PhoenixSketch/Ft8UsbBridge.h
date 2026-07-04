#ifndef FT8_USB_BRIDGE_H_
#define FT8_USB_BRIDGE_H_

#include <Arduino.h>

void Ft8UsbBridge_Init(float dspSampleRate);
bool Ft8UsbBridge_GetSamples(float *out, uint32_t outCount);
void Ft8UsbBridge_PutRxSamples(const float *in, uint32_t count);
void Ft8UsbBridge_DrainToUSB(void);
void Ft8UsbBridge_SetTxActive(bool on);

#endif