// Mock version to enable test harness to compile
#ifndef OPENAUDIO_ARDUINO_H
#define OPENAUDIO_ARDUINO_H
#include <stdint.h>
#include <stdio.h>

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

class AudioRecordQueue
{
    public:
        AudioRecordQueue(void){ }
        void begin(void) {
            clear();
            enabled = 1;
        }
        int available(void);
        void setChannel(uint8_t);
        void setChannel(uint8_t,int16_t*);
        uint8_t getChannel(void);
        void clear(void);
        int16_t * readBuffer(void);
        void freeBuffer(void);
        void end(void) {
            enabled = 0;
        }
        virtual void update(void);
    private:
        volatile uint8_t channel, enabled;
        volatile uint32_t head;
        int16_t *data;
};

class AudioPlayQueue
{
    public:
        void begin(void){
            fopened = false;
        }
        void end(void){
            fclose(fle);
        }
        int16_t *getBuffer(void);
        void playBuffer(void);
        void setName(char *fn);
    private:
        int16_t buf[128];
        FILE *fle;
        bool fopened;
};


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
        void frequency(float f) { }
        void amplitude(float f) { }
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
        void audioProcessorDisable(void){ }
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


#endif // OPENAUDIO_ARDUINO_H
