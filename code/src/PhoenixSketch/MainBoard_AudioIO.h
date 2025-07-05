#ifndef AUDIOIO_H
#define AUDIOIO_H
#include "SDT.h"

#ifdef TESTMODE
#include "../../test/AudioIO_mock.h"
#else
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <OpenAudio_ArduinoLibrary.h>
#endif

int SetI2SFreq(int freq);
void SetupAudio(void);
void UpdateAudioIOState(void);
ModeSm_StateId GetAudioPreviousState(void);


#endif // AUDIOIO_H