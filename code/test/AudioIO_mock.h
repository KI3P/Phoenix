#ifndef AUDIOIOMOCK_H
#define AUDIOIOMOCK_H

#include "SignalProcessing_mock.h"

#define LOW 0
#define HIGH 1
#define AUDIO_INPUT_MIC 1
#define AUDIO_INPUT_LINEIN 2

void AudioMemory(uint16_t mem);
void AudioMemory_F32(uint16_t mem);
void set_audioClock(int c0, int c1, int c2, bool b);
uint32_t CCM_CS1CDR_SAI1_CLK_PRED(int a);
uint32_t CCM_CS1CDR_SAI1_CLK_PODF(int a);
uint32_t CCM_CS2CDR_SAI2_CLK_PRED(int a);
uint32_t CCM_CS2CDR_SAI2_CLK_PODF(int a);
extern uint32_t CCM_CS1CDR;
extern uint32_t CCM_CS1CDR_SAI1_CLK_PRED_MASK;
extern uint32_t CCM_CS1CDR_SAI1_CLK_PODF_MASK;
extern uint32_t CCM_CS2CDR;
extern uint32_t CCM_CS2CDR_SAI2_CLK_PRED_MASK;
extern uint32_t CCM_CS2CDR_SAI2_CLK_PODF_MASK;

void digitalWrite(uint16_t pin, uint8_t val);
uint8_t digitalRead(uint16_t pin);
void pinMode(uint16_t pin, uint8_t val);
void serialprint(char *msg);
void serialprintBegin(char *fname);
void serialprintEnd(void);
void serialprint(float *value);

class AudioInputI2SQuad
{
    public:
        AudioInputI2SQuad(void){ }
        void begin(void) { }
        void end(void) {  }
};

class AudioOutputI2SQuad
{
    public:
        AudioOutputI2SQuad(void){ }
        void begin(void) { }
        void end(void) {  }
};

class AudioMixer4
{
    public:
        AudioMixer4(void){ }
        void begin(void) { }
        void end(void) {  }
        void gain(uint8_t channel, float32_t volume){
            gn = volume;
        }
    private:
        uint8_t gn;
};

class AudioSynthWaveformSine
{
    public:
        AudioSynthWaveformSine(void){ }
        void begin(void) { }
        void end(void) {  }
};

class AudioControlSGTL5000
{
    public:
        AudioControlSGTL5000(void){ }
        void begin(void) { }
        void end(void) {  }
        void micGain(uint32_t mic) { }
        void setAddress(uint8_t addr) { }
        void enable(void) { }
        void inputSelect(uint8_t input){ }
        void lineInLevel(uint8_t level){ }
        void lineOutLevel(uint8_t level){ }
        void adcHighPassFilterDisable(void){ }
        void volume(float32_t vol){ }
};

class AudioControlSGTL5000_Extended : public AudioControlSGTL5000
{
    public:
        AudioControlSGTL5000_Extended(void){ }
};

class AudioConnection
{
    public:
        AudioConnection(AudioInputI2SQuad a, int b, AudioMixer4 c, int d){ }
        AudioConnection(AudioMixer4 a, AudioRecordQueue b){ }
        AudioConnection(AudioSynthWaveformSine a, int b, AudioMixer4 c, int d){ }
        AudioConnection(AudioPlayQueue a, int b, AudioMixer4 c, int d){ }
        AudioConnection(AudioMixer4 a, int b, AudioOutputI2SQuad c, int d){ }
        void begin(void) { }
        void end(void) {  }
};



#endif // AUDIOIOMOCK_H