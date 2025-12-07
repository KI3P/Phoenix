// Mock version to enable test harness to compile
#ifndef OPENAUDIO_ARDUINO_H
#define OPENAUDIO_ARDUINO_H
#include <stdint.h>
#include <stdio.h>

// Audio source selection for simulator
enum AudioInputSource {
    AUDIO_SOURCE_COMPUTER,   // Live audio from computer's audio input
    AUDIO_SOURCE_TWO_TONE,   // Two-tone test signal (700Hz and 1900Hz at -48kHz)
    AUDIO_SOURCE_SINGLE_TONE // Single-tone test signal (1000Hz at -49kHz)
};

void setAudioInputSource(AudioInputSource source);
AudioInputSource getAudioInputSource(void);
const char* getAudioInputSourceName(void);

#ifdef USE_SDL_DISPLAY
// SDL2 Audio support - cross-platform (Linux, Windows, macOS)
bool SDL_Audio_Init(int sampleRate);
void SDL_Audio_Cleanup(void);
void SDL_Audio_QueueSamples(const int16_t* samples, int numSamples, uint8_t channel);
int SDL_Audio_ReadSamples(int16_t* samplesL, int16_t* samplesR, int numSamples);
bool SDL_Audio_InputAvailable(void);
size_t SDL_Audio_OutputBufferLevel(void);  // Returns samples buffered for output
bool SDL_Audio_OutputNeedsData(void);      // True if output buffer is below target level
#endif

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
            if (fopened) fclose(fle);
        }
        int16_t *getBuffer(void);
        void playBuffer(void);
        void setName(char *fn);
        void setAudioChannel(uint8_t ch) { audioChannel = ch; }
        uint8_t getAudioChannel(void) { return audioChannel; }
    private:
        int16_t buf[128];
        FILE *fle;
        bool fopened;
        uint8_t audioChannel = 0; // 0=left, 1=right
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
