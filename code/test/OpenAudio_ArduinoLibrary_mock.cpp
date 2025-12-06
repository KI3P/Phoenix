#include "../src/PhoenixSketch/SDT.h"
#include "OpenAudio_ArduinoLibrary.h"

#ifdef USE_SDL_DISPLAY
#include <SDL2/SDL.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstring>

// ============== Audio Output (Playback) ==============
// Ring buffer for stereo audio output - 1 second at ~115kHz (DSP throughput rate)
// Store interleaved stereo: L0, R0, L1, R1, ...
static const size_t AUDIO_OUT_BUFFER_SIZE = 120000 * 2;  // stereo samples
static int16_t audioOutBuffer[AUDIO_OUT_BUFFER_SIZE];
static std::atomic<size_t> outWritePos{0};
static std::atomic<size_t> outReadPos{0};
static SDL_AudioDeviceID audioOutDevice = 0;

// Temporary buffer to accumulate L samples until R arrives
static int16_t pendingL[128];
static int pendingLCount = 0;

// ============== Audio Input (Capture) ==============
// Ring buffer for stereo audio input - 1 second at 192kHz
static const size_t AUDIO_IN_BUFFER_SIZE = 192000;
static int16_t audioInBuffer_L[AUDIO_IN_BUFFER_SIZE];
static int16_t audioInBuffer_R[AUDIO_IN_BUFFER_SIZE];
static std::atomic<size_t> inWritePos{0};
static std::atomic<size_t> inReadPos{0};
static std::mutex inMutex;
static std::condition_variable inDataAvailable;
static SDL_AudioDeviceID audioInDevice = 0;

static int currentSampleRate = 192000;
static std::atomic<uint32_t> samplesPlayed{0};
static std::atomic<uint32_t> samplesCaptured{0};
static bool sdlAudioEnabled = false;

// Debug: track underruns
static std::atomic<uint32_t> underrunCount{0};
static std::atomic<uint32_t> callbackCount{0};
static std::atomic<uint32_t> samplesQueued{0};
static std::atomic<uint32_t> samplesConsumed{0};  // samples actually read by DSP

// Audio output callback - SDL calls this to get samples for playback
static void Phoenix_OutputCallback(void* userdata, Uint8* stream, int len) {
    int16_t* outStream = reinterpret_cast<int16_t*>(stream);
    int numSamples = len / sizeof(int16_t);  // Total samples (L+R interleaved)

    size_t readPos = outReadPos.load();
    size_t writePos = outWritePos.load();

    int samplesOutput = 0;
    int silenceOutput = 0;

    for (int i = 0; i < numSamples; i++) {
        if (readPos != writePos) {
            outStream[i] = audioOutBuffer[readPos];
            readPos = (readPos + 1) % AUDIO_OUT_BUFFER_SIZE;
            samplesOutput++;
        } else {
            // Buffer underrun - output silence
            outStream[i] = 0;
            silenceOutput++;
        }
    }
    outReadPos.store(readPos);
    samplesPlayed.fetch_add(samplesOutput / 2);

    if (silenceOutput > 0) {
        underrunCount.fetch_add(1);
    }
    callbackCount.fetch_add(1);
}

// Audio input callback - SDL calls this when captured samples are available
static void Phoenix_InputCallback(void* userdata, Uint8* stream, int len) {
    int16_t* inStream = reinterpret_cast<int16_t*>(stream);
    int numStereoSamples = len / (2 * sizeof(int16_t));

    size_t writePos = inWritePos.load();

    for (int i = 0; i < numStereoSamples; i++) {
        // Deinterleave stereo input: L, R, L, R, ... -> separate L and R buffers
        audioInBuffer_L[writePos] = inStream[i * 2];
        audioInBuffer_R[writePos] = inStream[i * 2 + 1];
        writePos = (writePos + 1) % AUDIO_IN_BUFFER_SIZE;
    }

    inWritePos.store(writePos);
    samplesCaptured.fetch_add(numStereoSamples);

    // Signal that new data is available
    inDataAvailable.notify_all();
}

bool SDL_Audio_Init(int sampleRate) {
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            fprintf(stderr, "SDL Audio init failed: %s\n", SDL_GetError());
            return false;
        }
    }

    currentSampleRate = sampleRate;

    // ---- Initialize Audio Output (Playback) ----
    // DSP runs at ~60% of real-time (192kHz), so effective rate is ~115kHz
    // Output at this rate to match DSP throughput (no decimation)
    int outputRate = 115200;

    SDL_AudioSpec outDesired, outObtained;
    SDL_zero(outDesired);
    outDesired.freq = outputRate;
    outDesired.format = AUDIO_S16SYS;
    outDesired.channels = 2;
    outDesired.samples = 1024;
    outDesired.callback = Phoenix_OutputCallback;
    outDesired.userdata = nullptr;

    // Allow sample rate flexibility for output
    audioOutDevice = SDL_OpenAudioDevice(nullptr, 0, &outDesired, &outObtained,
                                          SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (audioOutDevice == 0) {
        fprintf(stderr, "Failed to open audio output: %s\n", SDL_GetError());
        return false;
    }
    printf("SDL Audio Output: %d Hz, %d ch, %d samples buffer\n",
           outObtained.freq, outObtained.channels, outObtained.samples);

    // ---- Initialize Audio Input (Capture) ----
    // Must use full sample rate (192kHz) to match DSP expectations
    int captureRate = sampleRate;

    SDL_AudioSpec inDesired, inObtained;
    SDL_zero(inDesired);
    inDesired.freq = captureRate;
    inDesired.format = AUDIO_S16SYS;
    inDesired.channels = 2;
    inDesired.samples = 1024;
    inDesired.callback = Phoenix_InputCallback;
    inDesired.userdata = nullptr;

    // Allow sample rate flexibility for input
    audioInDevice = SDL_OpenAudioDevice(nullptr, 1, &inDesired, &inObtained,
                                         SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (audioInDevice == 0) {
        fprintf(stderr, "Failed to open audio input: %s\n", SDL_GetError());
        // Continue without input - just use mock data
        printf("Audio capture not available - using mock input data\n");
    } else {
        printf("SDL Audio Input: %d Hz, %d ch, %d samples buffer\n",
               inObtained.freq, inObtained.channels, inObtained.samples);
    }

    // Clear buffers
    memset(audioOutBuffer, 0, sizeof(audioOutBuffer));
    memset(audioInBuffer_L, 0, sizeof(audioInBuffer_L));
    memset(audioInBuffer_R, 0, sizeof(audioInBuffer_R));

    // Pre-fill output buffer with ~500ms of silence to absorb timing jitter
    // This prevents underruns during initial startup and DSP processing gaps
    int actualOutputRate = 115200;
    size_t prefillSamples = actualOutputRate / 2 * 2;  // 500ms stereo samples
    outWritePos.store(prefillSamples);
    outReadPos.store(0);
    pendingLCount = 0;
    inWritePos.store(0);
    inReadPos.store(0);
    samplesPlayed.store(0);
    samplesCaptured.store(0);
    samplesQueued.store(0);
    samplesConsumed.store(0);
    underrunCount.store(0);
    callbackCount.store(0);

    // Start audio devices
    SDL_PauseAudioDevice(audioOutDevice, 0);
    if (audioInDevice != 0) {
        SDL_PauseAudioDevice(audioInDevice, 0);
    }

    sdlAudioEnabled = true;
    return true;
}

void SDL_Audio_Cleanup(void) {
    sdlAudioEnabled = false;

    if (audioOutDevice != 0) {
        printf("SDL Audio: captured=%u consumed=%u queued=%u played=%u\n",
               samplesCaptured.load(), samplesConsumed.load(), samplesQueued.load(), samplesPlayed.load());
        printf("SDL Audio: %u callbacks, %u underruns (%.1f%%)\n",
               callbackCount.load(), underrunCount.load(),
               callbackCount.load() > 0 ? 100.0 * underrunCount.load() / callbackCount.load() : 0.0);
        SDL_CloseAudioDevice(audioOutDevice);
        audioOutDevice = 0;
    }
    if (audioInDevice != 0) {
        SDL_CloseAudioDevice(audioInDevice);
        audioInDevice = 0;
    }
}

void SDL_Audio_QueueSamples(const int16_t* samples, int numSamples, uint8_t channel) {
    if (audioOutDevice == 0) return;

    if (channel == 0) {
        // Left channel - save samples for later interleaving
        memcpy(pendingL, samples, numSamples * sizeof(int16_t));
        pendingLCount = numSamples;
    } else {
        // Right channel - interleave with pending L and write to buffer
        if (pendingLCount != numSamples) {
            // Mismatch - shouldn't happen, but handle gracefully
            pendingLCount = 0;
            return;
        }

        size_t pos = outWritePos.load();

        // Write all samples (no decimation - SDL will resample as needed)
        for (int i = 0; i < numSamples; i++) {
            audioOutBuffer[pos] = pendingL[i];  // L
            pos = (pos + 1) % AUDIO_OUT_BUFFER_SIZE;
            audioOutBuffer[pos] = samples[i];   // R
            pos = (pos + 1) % AUDIO_OUT_BUFFER_SIZE;
        }

        outWritePos.store(pos);
        pendingLCount = 0;
        samplesQueued.fetch_add(numSamples);
    }
}

// Read captured audio samples - non-blocking (caller checks available() first)
// Returns number of samples actually read per channel
int SDL_Audio_ReadSamples(int16_t* samplesL, int16_t* samplesR, int numSamples) {
    if (audioInDevice == 0 || !sdlAudioEnabled) return 0;

    size_t readPos = inReadPos.load();
    size_t writePos = inWritePos.load();

    // Calculate available samples
    size_t available;
    if (writePos >= readPos) {
        available = writePos - readPos;
    } else {
        available = AUDIO_IN_BUFFER_SIZE - readPos + writePos;
    }

    // Don't read more than available
    int toRead = (available < (size_t)numSamples) ? available : numSamples;
    if (toRead == 0) return 0;

    // Copy samples
    for (int i = 0; i < toRead; i++) {
        samplesL[i] = audioInBuffer_L[readPos];
        samplesR[i] = audioInBuffer_R[readPos];
        readPos = (readPos + 1) % AUDIO_IN_BUFFER_SIZE;
    }

    inReadPos.store(readPos);
    samplesConsumed.fetch_add(toRead);
    return toRead;
}

bool SDL_Audio_InputAvailable(void) {
    return audioInDevice != 0 && sdlAudioEnabled;
}

#endif // USE_SDL_DISPLAY

#include "mock_R_data_int.c"
#include "mock_L_data_int.c"
#include "mock_L_data_int_1khz.c"
#include "mock_R_data_int_1khz.c"

// Helper to get number of samples available in SDL input buffer
#ifdef USE_SDL_DISPLAY
static size_t SDL_Audio_SamplesAvailable(void) {
    if (audioInDevice == 0 || !sdlAudioEnabled) return 0;

    size_t readPos = inReadPos.load();
    size_t writePos = inWritePos.load();

    if (writePos >= readPos) {
        return writePos - readPos;
    } else {
        return AUDIO_IN_BUFFER_SIZE - readPos + writePos;
    }
}
#endif

int AudioRecordQueue::available(void) {
#ifdef USE_SDL_DISPLAY
    if (SDL_Audio_InputAvailable()) {
        // Return actual number of BLOCKS available (samples / BUFFER_SIZE)
        // DSP checks if available() > N_BLOCKS before reading
        size_t samplesAvailable = SDL_Audio_SamplesAvailable();
        return samplesAvailable / BUFFER_SIZE;
    }
#endif
    // Fallback to mock behavior
    int blocks_available = 4*2048/BUFFER_SIZE;
    int answer = (blocks_available-head+1)*BUFFER_SIZE;
    if (answer >= 100) answer = 99;
    return answer;
}

void AudioRecordQueue::clear(void) {
    head = 0;
}

void AudioRecordQueue::setChannel(uint8_t chan) {
    channel = chan;
    if (chan == 1) {
        data = R_mock;
    }
    if (chan == 0) {
        data = L_mock;
    }
    if (chan == 3) {
        data = R_mock_1khz;
    }
    if (chan == 2) {
        data = L_mock_1khz;
    }
}

void AudioRecordQueue::setChannel(uint8_t chan, int16_t *dataChan) {
    channel = chan;
    data = dataChan;
}

uint8_t AudioRecordQueue::getChannel(void) {
    return channel;
}

int16_t* AudioRecordQueue::readBuffer(void) {
#ifdef USE_SDL_DISPLAY
    if (SDL_Audio_InputAvailable()) {
        // Static buffers for L and R channels
        // We read both channels together when L is requested,
        // then return cached R data when R is requested
        static int16_t sdlReadBuf_L[BUFFER_SIZE];
        static int16_t sdlReadBuf_R[BUFFER_SIZE];
        static bool rDataReady = false;

        if (channel == 0 || channel == 2) {
            // Left channel request - read fresh data for both L and R
            SDL_Audio_ReadSamples(sdlReadBuf_L, sdlReadBuf_R, BUFFER_SIZE);
            rDataReady = true;
            return sdlReadBuf_L;
        } else {
            // Right channel request - return cached R data
            if (rDataReady) {
                rDataReady = false;
                return sdlReadBuf_R;
            } else {
                // R requested before L - read fresh data
                SDL_Audio_ReadSamples(sdlReadBuf_L, sdlReadBuf_R, BUFFER_SIZE);
                return sdlReadBuf_R;
            }
        }
    }
#endif
    // Fallback to mock data
    int16_t * ptr = &data[head*BUFFER_SIZE];
    head += 1;
    int blocks_available = 4*2048/BUFFER_SIZE;
    if (head == (blocks_available+1)) head = 0;
    return ptr;
}

void AudioRecordQueue::freeBuffer(void) {
    return;
}

void AudioRecordQueue::update(void) {
    return;
}

int16_t *AudioPlayQueue::getBuffer(void){
    return buf;
}

void AudioPlayQueue::setName(char *fn){
    if (fn != nullptr) fle = fopen(fn, "w");
    else fle = nullptr;
    fopened = (fle != nullptr);
}

void AudioPlayQueue::playBuffer(void){
#ifdef USE_SDL_DISPLAY
    // Send audio to SDL for playback
    SDL_Audio_QueueSamples(buf, 128, audioChannel);
#endif
    // Also write to file if enabled (for debugging/analysis)
    if (fopened){
        for (size_t k=0; k<128; k++){
            fprintf(fle,"%d\n",buf[k]);
        }
    }
    return;
}

uint32_t CCM_CS1CDR;
uint32_t CCM_CS1CDR_SAI1_CLK_PRED_MASK;
uint32_t CCM_CS1CDR_SAI1_CLK_PODF_MASK;
uint32_t CCM_CS2CDR;
uint32_t CCM_CS2CDR_SAI2_CLK_PRED_MASK;
uint32_t CCM_CS2CDR_SAI2_CLK_PODF_MASK;

void AudioMemory(uint16_t mem){}
void AudioMemory_F32(uint16_t mem){}
void set_audioClock(int c0, int c1, int c2, bool b){}
uint32_t CCM_CS1CDR_SAI1_CLK_PRED(int a){return 0;}
uint32_t CCM_CS1CDR_SAI1_CLK_PODF(int a){return 0;}
uint32_t CCM_CS2CDR_SAI2_CLK_PRED(int a){return 0;}
uint32_t CCM_CS2CDR_SAI2_CLK_PODF(int a){return 0;}
