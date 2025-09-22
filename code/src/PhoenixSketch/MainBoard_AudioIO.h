#ifndef AUDIOIO_H
#define AUDIOIO_H
#include "SDT.h"

#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <OpenAudio_ArduinoLibrary.h>

#define SIDETONE_FREQUENCY 100

int SetI2SFreq(int freq);
void InitializeAudio(void);
void UpdateAudioIOState(void);
ModeSm_StateId GetAudioPreviousState(void);
void UpdateTransmitAudioGain(void);

#endif // AUDIOIO_H