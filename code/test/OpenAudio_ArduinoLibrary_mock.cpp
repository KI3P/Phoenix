#include "../src/PhoenixSketch/SDT.h"
#include "../src/PhoenixSketch/DSP_FFT.h"
#include "OpenAudio_ArduinoLibrary.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

// ============== Audio Source Selection ==============
// Default: MOCK_DATA for unit tests, TWO_TONE for simulator with SDL
#ifdef USE_SDL_DISPLAY
static AudioInputSource currentAudioSource = AUDIO_SOURCE_TWO_TONE;
#else
static AudioInputSource currentAudioSource = AUDIO_SOURCE_MOCK_DATA;
#endif

// Forward declaration for tone timing reset
static bool toneTimerInitialized = false;

void setAudioInputSource(AudioInputSource source) {
    currentAudioSource = source;
    // Reset tone timing when switching to a tone source
    if (source == AUDIO_SOURCE_TWO_TONE || source == AUDIO_SOURCE_SINGLE_TONE ||
        source == AUDIO_SOURCE_RXIQ_LSB || source == AUDIO_SOURCE_RXIQ_USB) {
        toneTimerInitialized = false;  // Will reinitialize on next use
    }
}

AudioInputSource getAudioInputSource(void) {
    return currentAudioSource;
}

const char* getAudioInputSourceName(void) {
    switch (currentAudioSource) {
        case AUDIO_SOURCE_COMPUTER:    return "Computer Audio";
        case AUDIO_SOURCE_TWO_TONE:    return "Two-Tone (700/1900Hz @ 48kHz)";
        case AUDIO_SOURCE_SINGLE_TONE: return "Single-Tone (1kHz @ 49kHz)";
        case AUDIO_SOURCE_RXIQ_LSB:    return "RX IQ tones (LSB)";
        case AUDIO_SOURCE_RXIQ_USB:    return "RX IQ tones (USB)";
        case AUDIO_SOURCE_FEEDBACK:    return "Feedback (output to input)";
        case AUDIO_SOURCE_MOCK_DATA:   return "Mock Data (unit tests)";
        default:                       return "Unknown";
    }
}

// ============== Direct Feedback Buffer ==============
// Simple ring buffer for feedback: Q_out_L_Ex → Q_in_L, Q_out_R_Ex → Q_in_R
// This provides a direct path from transmit IQ output to receive IQ input.
// A frequency shift (FreqShiftMFs4) is applied to simulate the RF path.

static const int FEEDBACK_BLOCK_SIZE = 128;
static const int FEEDBACK_MAX_BLOCKS = 100;  // ~65ms of audio at 192kHz

class DirectFeedbackBuffer {
public:
    DirectFeedbackBuffer() : writeBlock(0), readBlock(0) {
        memset(buffer, 0, sizeof(buffer));
    }

    void clear() {
        writeBlock = 0;
        readBlock = 0;
    }

    // Write a block of samples (called by AudioPlayQueue::playBuffer)
    void writeBuffer(const int16_t* samples) {
        memcpy(buffer[writeBlock], samples, FEEDBACK_BLOCK_SIZE * sizeof(int16_t));
        writeBlock = (writeBlock + 1) % FEEDBACK_MAX_BLOCKS;
        // If we've caught up to the read pointer, advance it (overwrite oldest)
        if (writeBlock == readBlock) {
            readBlock = (readBlock + 1) % FEEDBACK_MAX_BLOCKS;
        }
    }

    // Get number of blocks available to read
    int available() const {
        if (writeBlock >= readBlock) {
            return writeBlock - readBlock;
        } else {
            return FEEDBACK_MAX_BLOCKS - readBlock + writeBlock;
        }
    }

    // Read a block of samples (called by AudioRecordQueue::readBuffer)
    int16_t* readBuffer() {
        if (readBlock == writeBlock) {
            // Buffer empty - return zeros
            static int16_t emptyBuffer[FEEDBACK_BLOCK_SIZE] = {0};
            return emptyBuffer;
        }
        int16_t* ptr = buffer[readBlock];
        readBlock = (readBlock + 1) % FEEDBACK_MAX_BLOCKS;
        return ptr;
    }

private:
    int16_t buffer[FEEDBACK_MAX_BLOCKS][FEEDBACK_BLOCK_SIZE];
    volatile int writeBlock;
    volatile int readBlock;
};

// Global feedback buffers: L_Ex output → L input, R_Ex output → R input
static DirectFeedbackBuffer g_feedbackL;  // Q_out_L_Ex → Q_in_L
static DirectFeedbackBuffer g_feedbackR;  // Q_out_R_Ex → Q_in_R

// ============== Frequency-Shifted Feedback Processing ==============
// Buffers I channel (Q_out_L_Ex), waits for Q channel (Q_out_R_Ex),
// combines into DataBlock, applies FreqShiftMFs4, then writes to feedback buffers.

class FreqShiftedFeedback {
public:
    FreqShiftedFeedback() : pendingI(false) {
        memset(iBuffer, 0, sizeof(iBuffer));
        // Initialize DataBlock
        dataBlock.N = FEEDBACK_BLOCK_SIZE;
        dataBlock.sampleRate_Hz = 192000;
        dataBlock.I = iFloat;
        dataBlock.Q = qFloat;
    }

    // Called when Q_out_L_Ex (channel 2) writes - buffer the I channel
    void writeI(const int16_t* samples) {
        memcpy(iBuffer, samples, FEEDBACK_BLOCK_SIZE * sizeof(int16_t));
        pendingI = true;
    }

    // Called when Q_out_R_Ex (channel 3) writes - combine, shift, and write to feedback
    void writeQ(const int16_t* samples) {
        if (!pendingI) {
            // Q arrived before I - just buffer Q and wait
            // This shouldn't normally happen if L is always written before R
            return;
        }

        // Convert int16_t to float32_t for DataBlock
        // Swap I and Q so that sideband is correct and apply imperfections too
        for (int i = 0; i < FEEDBACK_BLOCK_SIZE; i++) {
            iFloat[i] = 1.1*(float32_t)samples[i] + 150.2;
            qFloat[i] = (float32_t)iBuffer[i] + 67.8;
        }

        // Apply frequency shift (Fs/4 = 48kHz at 192kHz sample rate)
        FreqShiftMFs4(&dataBlock);

        // Convert back to int16_t and write to feedback buffers
        int16_t shiftedI[FEEDBACK_BLOCK_SIZE];
        int16_t shiftedQ[FEEDBACK_BLOCK_SIZE];
        for (int i = 0; i < FEEDBACK_BLOCK_SIZE; i++) {
            // Clamp to int16_t range
            float32_t iVal = iFloat[i];
            float32_t qVal = qFloat[i];
            if (iVal > 32767.0f) iVal = 32767.0f;
            if (iVal < -32768.0f) iVal = -32768.0f;
            if (qVal > 32767.0f) qVal = 32767.0f;
            if (qVal < -32768.0f) qVal = -32768.0f;
            shiftedI[i] = (int16_t)iVal;
            shiftedQ[i] = (int16_t)qVal;
        }

        // Write shifted data to feedback buffers
        g_feedbackL.writeBuffer(shiftedI);
        g_feedbackR.writeBuffer(shiftedQ);

        pendingI = false;
    }

private:
    int16_t iBuffer[FEEDBACK_BLOCK_SIZE];   // Buffered I channel samples
    float32_t iFloat[FEEDBACK_BLOCK_SIZE];  // Float buffer for DataBlock I
    float32_t qFloat[FEEDBACK_BLOCK_SIZE];  // Float buffer for DataBlock Q
    DataBlock dataBlock;                     // DataBlock for FreqShiftMFs4
    bool pendingI;                           // True if I channel is buffered, waiting for Q
};

// Global frequency-shifted feedback processor
static FreqShiftedFeedback g_freqShiftFeedback;

// ============== Tone Generator ==============
// Sample rate is 192kHz
// For I/Q signal at frequency f: I = cos(2*pi*f*t), Q = -sin(2*pi*f*t)
// (negative sin for USB representation where positive frequency = lower sideband)
static const double TONE_SAMPLE_RATE = 192000.0;
static const double TONE_TWO_PI = 2.0 * M_PI;

// Phase accumulators for continuous tone generation
static double tonePhase1 = 0.0;  // For first tone (or single tone)
static double tonePhase2 = 0.0;  // For second tone (two-tone only)

// Generate random noise in range [-1.0, 1.0]
static inline double randomNoise() {
    return (2.0 * rand() / RAND_MAX) - 1.0;
}

// ============== Tone Generator Timing Control ==============
// Track samples available based on elapsed time at 192kHz
static std::chrono::steady_clock::time_point toneStartTime;
static uint64_t toneSamplesConsumed = 0;

// Initialize or reset the tone timing
static void initToneTiming() {
    toneStartTime = std::chrono::steady_clock::now();
    toneSamplesConsumed = 0;
    toneTimerInitialized = true;
}

// Get number of samples that should be available based on elapsed time
static size_t getToneSamplesAvailable() {
    if (!toneTimerInitialized) {
        initToneTiming();
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - toneStartTime);

    // Calculate total samples that should have been generated at 192kHz
    // samples = time_in_seconds * 192000
    uint64_t totalSamplesExpected = (elapsed.count() * 192000) / 1000000;

    // Available = expected - consumed
    if (totalSamplesExpected > toneSamplesConsumed) {
        return totalSamplesExpected - toneSamplesConsumed;
    }
    return 0;
}

// Generate I/Q samples for two-tone test signal
// Tones at 700Hz and 1900Hz offset from 48kHz carrier
static void generateTwoToneSamples(int16_t* samplesI, int16_t* samplesQ, int numSamples) {
    // Audio offsets from 48kHz
    const double audioFreq1 = 700.0;   // Hz
    const double audioFreq2 = 1900.0;  // Hz
    // RF carrier offset
    const double carrierFreq = 48000.0;  // Hz

    // Combined frequencies
    const double freq1 = carrierFreq + audioFreq1;  // 48700 Hz
    const double freq2 = carrierFreq + audioFreq2;  // 49900 Hz

    const double phaseInc1 = TONE_TWO_PI * freq1 / TONE_SAMPLE_RATE;
    const double phaseInc2 = TONE_TWO_PI * freq2 / TONE_SAMPLE_RATE;

    // Amplitude for each tone (half amplitude so sum doesn't clip)
    const double amplitude = 1380.0;
    // Noise amplitude is 1/20th of signal amplitude
    const double noiseAmp = amplitude / 20.0;

    for (int i = 0; i < numSamples; i++) {
        // Sum of two tones plus noise
        double I = amplitude * (cos(tonePhase1) + cos(tonePhase2)) + noiseAmp * randomNoise();
        double Q = amplitude * (-sin(tonePhase1) + -sin(tonePhase2)) + noiseAmp * randomNoise();

        samplesI[i] = (int16_t)I;
        samplesQ[i] = (int16_t)Q;

        tonePhase1 += phaseInc1;
        tonePhase2 += phaseInc2;

        // Keep phases in reasonable range
        if (tonePhase1 > TONE_TWO_PI) tonePhase1 -= TONE_TWO_PI;
        if (tonePhase1 < -TONE_TWO_PI) tonePhase1 += TONE_TWO_PI;
        if (tonePhase2 > TONE_TWO_PI) tonePhase2 -= TONE_TWO_PI;
        if (tonePhase2 < -TONE_TWO_PI) tonePhase2 += TONE_TWO_PI;
    }
}

// Generate I/Q samples for single-tone test signal
// Tone at -49kHz (1kHz below -48kHz, will appear at 1kHz audio)
static void generateSingleToneSamples(int16_t* samplesI, int16_t* samplesQ, int numSamples) {
    const double freq = 49000.0;  // Hz (48kHz + 1kHz = 49kHz)
    const double phaseInc = TONE_TWO_PI * freq / TONE_SAMPLE_RATE;
    const double amplitude = 1380.0;  // Full scale single tone. 1380 = S9 tone (determined experimentally).
    // Noise amplitude is 1/20th of signal amplitude
    const double noiseAmp = amplitude / 20.0;

    for (int i = 0; i < numSamples; i++) {
        double I = amplitude * cos(tonePhase1) + noiseAmp * randomNoise();
        double Q = amplitude * -sin(tonePhase1) + noiseAmp * randomNoise();

        samplesI[i] = (int16_t)I;
        samplesQ[i] = (int16_t)Q;

        tonePhase1 += phaseInc;

        // Keep phase in reasonable range
        if (tonePhase1 > TONE_TWO_PI) tonePhase1 -= TONE_TWO_PI;
        if (tonePhase1 < -TONE_TWO_PI) tonePhase1 += TONE_TWO_PI;
    }
}

// Generate I/Q samples for RX LSB IQ calibration case
// Tone at 48kHz carrier, with a small error included
static void generateRXIQLSBSamples(int16_t* samplesI, int16_t* samplesQ, int numSamples) {
    const double freq = 48000.0;  // Hz
    const double phaseInc = TONE_TWO_PI * freq / TONE_SAMPLE_RATE;
    const double amplitude = 1380.0;  // Full scale single tone. 1380 = S9 tone (determined experimentally).
    // Noise amplitude is 1/40th of signal amplitude
    const double noiseAmp = amplitude / 40.0;

    for (int i = 0; i < numSamples; i++) {
        double I = 0.9*amplitude * cos(tonePhase1) + noiseAmp * randomNoise();
        double Q = amplitude * -sin(tonePhase1) + noiseAmp * randomNoise();

        samplesI[i] = (int16_t)I;
        samplesQ[i] = (int16_t)Q;

        tonePhase1 += phaseInc;

        // Keep phase in reasonable range
        if (tonePhase1 > TONE_TWO_PI) tonePhase1 -= TONE_TWO_PI;
        if (tonePhase1 < -TONE_TWO_PI) tonePhase1 += TONE_TWO_PI;
    }
}

// Generate I/Q samples for RX USB IQ calibration case
// Tone at -48kHz carrier, with a small error included
static void generateRXIQUSBSamples(int16_t* samplesI, int16_t* samplesQ, int numSamples) {
    const double freq = -48000.0;  // Hz
    const double phaseInc = TONE_TWO_PI * freq / TONE_SAMPLE_RATE;
    const double amplitude = 1380.0;  // Full scale single tone. 1380 = S9 tone (determined experimentally).
    // Noise amplitude is 1/40th of signal amplitude
    const double noiseAmp = amplitude / 40.0;

    for (int i = 0; i < numSamples; i++) {
        double I = 0.9*amplitude * cos(tonePhase1) + noiseAmp * randomNoise();
        double Q = amplitude * -sin(tonePhase1) + noiseAmp * randomNoise();

        samplesI[i] = (int16_t)I;
        samplesQ[i] = (int16_t)Q;

        tonePhase1 += phaseInc;

        // Keep phase in reasonable range
        if (tonePhase1 > TONE_TWO_PI) tonePhase1 -= TONE_TWO_PI;
        if (tonePhase1 < -TONE_TWO_PI) tonePhase1 += TONE_TWO_PI;
    }
}

#ifdef USE_SDL_DISPLAY
#include <SDL2/SDL.h>
#include <condition_variable>

// ============== Audio Output (Playback) ==============
// Ring buffer for stereo audio output - 1 second at 48kHz (downsampled from 192kHz)
// Store interleaved stereo: L0, R0, L1, R1, ...
static const size_t AUDIO_OUT_BUFFER_SIZE = 48000 * 2;  // stereo samples
static const int DOWNSAMPLE_FACTOR = 4;  // 192kHz -> 48kHz
static int16_t audioOutBuffer[AUDIO_OUT_BUFFER_SIZE];
static std::atomic<size_t> outWritePos{0};
static std::atomic<size_t> outReadPos{0};
static SDL_AudioDeviceID audioOutDevice = 0;

// Temporary buffer to accumulate L samples until R arrives
static int16_t pendingL[128];
static int pendingLCount = 0;

// ============== IQ Output Buffer (for Feedback) ==============
// Separate ring buffer for IQ output from PlayIQData (channels 2/3)
// This is at 192kHz (no downsampling) for feedback loopback
static const size_t IQ_OUT_BUFFER_SIZE = 192000 * 2;  // 1 second stereo at 192kHz
static int16_t iqOutBuffer[IQ_OUT_BUFFER_SIZE];
static std::atomic<size_t> iqOutWritePos{0};
static std::atomic<size_t> iqOutReadPos{0};

// Temporary buffer to accumulate I samples until Q arrives
static int16_t pendingI[128];
static int pendingICount = 0;

// Feedback read position - tracks where we are in the IQ output buffer
static std::atomic<size_t> feedbackReadPos{0};

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
static std::atomic<bool> audioPlaybackStarted{false};  // Track if we've started playback

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
    // DSP output is at 192kHz, downsample to 48kHz for playback
    int outputRate = 48000;

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
    memset(iqOutBuffer, 0, sizeof(iqOutBuffer));

    // No prefill - we'll start playback once DSP has produced enough data
    outWritePos.store(0);
    outReadPos.store(0);
    pendingLCount = 0;
    iqOutWritePos.store(0);
    iqOutReadPos.store(0);
    pendingICount = 0;
    feedbackReadPos.store(0);
    inWritePos.store(0);
    inReadPos.store(0);
    samplesPlayed.store(0);
    samplesCaptured.store(0);
    samplesQueued.store(0);
    samplesConsumed.store(0);
    underrunCount.store(0);
    callbackCount.store(0);
    audioPlaybackStarted.store(false);

    // Start audio input immediately (we need to receive samples)
    if (audioInDevice != 0) {
        SDL_PauseAudioDevice(audioInDevice, 0);
    }
    // Keep audio output paused - will start once buffer has enough data
    SDL_PauseAudioDevice(audioOutDevice, 1);  // 1 = paused

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

    // Channels 0/1: Audio output (downsampled to 48kHz for speaker playback)
    // Channels 2/3: IQ output (full 192kHz for feedback loopback)
    if (channel == 0) {
        // Left audio channel - save samples for later interleaving
        memcpy(pendingL, samples, numSamples * sizeof(int16_t));
        pendingLCount = numSamples;
    } else if (channel == 1) {
        // Right audio channel - interleave with pending L and write to buffer
        if (pendingLCount != numSamples) {
            // Mismatch - shouldn't happen, but handle gracefully
            pendingLCount = 0;
            return;
        }

        size_t pos = outWritePos.load();

        // Downsample from 192kHz to 48kHz (factor of 4)
        // Simple decimation - take every 4th sample
        // For better quality, could add a low-pass filter before decimation
        for (int i = 0; i < numSamples; i += DOWNSAMPLE_FACTOR) {
            audioOutBuffer[pos] = pendingL[i];  // L
            pos = (pos + 1) % AUDIO_OUT_BUFFER_SIZE;
            audioOutBuffer[pos] = samples[i];   // R
            pos = (pos + 1) % AUDIO_OUT_BUFFER_SIZE;
        }

        outWritePos.store(pos);
        pendingLCount = 0;
        samplesQueued.fetch_add(numSamples / DOWNSAMPLE_FACTOR);

        // Start playback once we've buffered ~300ms of audio at 48kHz
        // This gives us headroom to absorb timing variations
        if (!audioPlaybackStarted.load()) {
            size_t writePos = outWritePos.load();
            size_t readPos = outReadPos.load();
            size_t buffered = (writePos >= readPos) ? (writePos - readPos)
                                                    : (AUDIO_OUT_BUFFER_SIZE - readPos + writePos);
            // 300ms at 48kHz stereo = 14400 stereo samples = 28800 buffer entries
            if (buffered >= 28800) {
                SDL_PauseAudioDevice(audioOutDevice, 0);  // 0 = unpaused
                audioPlaybackStarted.store(true);
                printf("SDL Audio: Started playback after buffering %zu samples (%.0fms)\n",
                       buffered/2, (buffered/2) * 1000.0 / 48000);
            }
        }
    } else if (channel == 2) {
        // I channel (IQ output) - save samples for later interleaving
        memcpy(pendingI, samples, numSamples * sizeof(int16_t));
        pendingICount = numSamples;
    } else if (channel == 3) {
        // Q channel (IQ output) - interleave with pending I and write to IQ buffer
        if (pendingICount != numSamples) {
            // Mismatch - shouldn't happen, but handle gracefully
            pendingICount = 0;
            return;
        }

        size_t pos = iqOutWritePos.load();

        // Write at full 192kHz (no downsampling) for feedback
        for (int i = 0; i < numSamples; i++) {
            iqOutBuffer[pos] = pendingI[i];  // I
            pos = (pos + 1) % IQ_OUT_BUFFER_SIZE;
            iqOutBuffer[pos] = samples[i];   // Q
            pos = (pos + 1) % IQ_OUT_BUFFER_SIZE;
        }

        iqOutWritePos.store(pos);
        pendingICount = 0;
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

// Returns the number of samples currently buffered for output (mono sample count)
size_t SDL_Audio_OutputBufferLevel(void) {
    size_t writePos = outWritePos.load();
    size_t readPos = outReadPos.load();
    if (writePos >= readPos) {
        return (writePos - readPos) / 2;  // Divide by 2 for mono count (buffer is stereo interleaved)
    } else {
        return (AUDIO_OUT_BUFFER_SIZE - readPos + writePos) / 2;
    }
}

// Target buffer level in samples (at 48kHz output rate)
// We want ~250ms of audio buffered to smooth out timing variations
// Each DSP cycle produces 512 samples (10.67ms at 48kHz)
static const size_t TARGET_BUFFER_LEVEL = 48000 / 4;  // 250ms at 48kHz = 12000 samples

// Returns true if output buffer needs more data (below target level)
bool SDL_Audio_OutputNeedsData(void) {
    if (!audioPlaybackStarted.load()) {
        // Before playback starts, always accept data to fill initial buffer
        return true;
    }
    // Always return true - let DSP run as fast as it can, buffer will regulate itself
    return true;
}

// ============== Feedback Mode ==============
// Reads IQ samples from PlayIQData output and feeds them back as input
// This creates a loopback for TX IQ calibration testing

// Generate I/Q samples from feedback (IQ output buffer from PlayIQData)
// Reads I/Q samples directly at 192kHz - no resampling or modulation needed
static void generateFeedbackSamples(int16_t* samplesI, int16_t* samplesQ, int numSamples) {
    size_t readPos = feedbackReadPos.load();
    size_t writePos = iqOutWritePos.load();

    // Calculate available I/Q sample pairs
    size_t available;
    if (writePos >= readPos) {
        available = (writePos - readPos) / 2;  // Divide by 2 for I/Q pairs
    } else {
        available = (IQ_OUT_BUFFER_SIZE - readPos + writePos) / 2;
    }

    int samplesRead = 0;

    // Read I/Q pairs directly from the IQ output buffer
    for (int i = 0; i < numSamples && samplesRead < (int)available; i++) {
        samplesI[i] = iqOutBuffer[readPos];
        readPos = (readPos + 1) % IQ_OUT_BUFFER_SIZE;
        samplesQ[i] = iqOutBuffer[readPos];
        readPos = (readPos + 1) % IQ_OUT_BUFFER_SIZE;
        samplesRead++;
    }

    feedbackReadPos.store(readPos);

    // Fill remaining with silence if not enough samples
    for (int i = samplesRead; i < numSamples; i++) {
        samplesI[i] = 0;
        samplesQ[i] = 0;
    }
}

// Check how many samples are available for feedback
static size_t getFeedbackSamplesAvailable() {
    size_t readPos = feedbackReadPos.load();
    size_t writePos = iqOutWritePos.load();

    size_t available;
    if (writePos >= readPos) {
        available = (writePos - readPos) / 2;  // I/Q pairs at 192kHz
    } else {
        available = (IQ_OUT_BUFFER_SIZE - readPos + writePos) / 2;
    }

    // Already at 192kHz, no conversion needed
    return available;
}

#endif // USE_SDL_DISPLAY

#include "mock_R_data_int.c"
#include "mock_L_data_int.c"
#include "mock_L_data_int_1khz.c"
#include "mock_R_data_int_1khz.c"

// ============== Oscillator-Driven Mode for TX IQ Calibration ==============
// Sample rate is 192kHz, block size is 128 samples
static const double OSC_SAMPLE_RATE = 192000.0;
static const double OSC_TWO_PI = 2.0 * M_PI;

// ============== Synchronized L/R Oscillator Generator ==============
// This generates both L and R samples together with a single phase accumulator
// to ensure phase continuity and correct frequency. We use I buffer for the left 
// channel and Q for the right channel.

class SynchronizedOscillatorGenerator {
public:
    static constexpr int BLOCK_SIZE = 128;
    static constexpr int MAX_BLOCKS = 1500;

    SynchronizedOscillatorGenerator() :
        oscillatorSource(nullptr),
        sampleIndex(0),
        writeBlock(0),
        readBlockI(0),
        readBlockQ(0),
        lastGenerateTime(0),
        enabled(false)
    {
        memset(bufferI, 0, sizeof(bufferI));
        memset(bufferQ, 0, sizeof(bufferQ));
    }

    void setSource(AudioSynthWaveformSine* osc) {
        oscillatorSource = osc;
        sampleIndex = 0;
        writeBlock = 0;
        readBlockI = 0;
        readBlockQ = 0;
        enabled = (osc != nullptr);

        // Initialize timing and pre-generate initial blocks
        if (enabled) {
            auto now = std::chrono::steady_clock::now();
            lastGenerateTime = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();
            // Pre-generate 20 blocks (~13ms of audio) to ensure data is always available
            preGenerateBlocks(20);
        } else {
            lastGenerateTime = 0;
        }
    }

    // Pre-generate a specified number of blocks immediately (not time-based)
    void preGenerateBlocks(int numBlocks) {
        if (!enabled || oscillatorSource == nullptr) {
            return;
        }

        float freq = oscillatorSource->getFrequency();
        float amp = oscillatorSource->getAmplitude();

        // Convert amplitude - the oscillator amplitude is set as amp/500 in InitializeAudio
        double amplitude = amp * 500.0 * 30.0;
        // Convert frequency
        freq = freq * 192000.0f / AUDIO_SAMPLE_RATE_EXACT;

        if (amplitude > 32767.0) amplitude = 32767.0;
        const double noiseAmp = amplitude / 20.0;
        double phaseInc = OSC_TWO_PI * freq / OSC_SAMPLE_RATE;

        for (int blk = 0; blk < numBlocks; blk++) {
            uint32_t nextWriteBlock = (writeBlock + 1) % MAX_BLOCKS;
            if (nextWriteBlock == readBlockI || nextWriteBlock == readBlockQ) {
                break;  // Buffer full
            }

            int16_t* blockL = bufferI[writeBlock];
            int16_t* blockR = bufferQ[writeBlock];

            for (int i = 0; i < BLOCK_SIZE; i++) {
                double theta = phaseInc * sampleIndex;
                blockL[i] = (int16_t)(amplitude * cos(theta) + noiseAmp * randomNoise());
                blockR[i] = blockL[i];
                sampleIndex++;
                if (sampleIndex >= 192000) {
                    sampleIndex = 0;
                }
            }
            writeBlock = nextWriteBlock;
        }
    }

    void clear() {
        sampleIndex = 0;
        writeBlock = 0;
        readBlockI = 0;
        readBlockQ = 0;
        lastGenerateTime = 0;
    }

    bool isEnabled() const { return enabled && oscillatorSource != nullptr; }

    // Generate synchronized I/Q samples based on elapsed time
    void generateSamples() {
        if (!enabled || oscillatorSource == nullptr) {
            return;
        }

        // Get current time in microseconds
        auto now = std::chrono::steady_clock::now();
        auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();

        // Initialize timing on first call
        if (lastGenerateTime == 0) {
            lastGenerateTime = nowUs;
            return;
        }

        // Calculate how many samples should have been generated since last call
        uint64_t elapsedUs = nowUs - lastGenerateTime;
        uint64_t samplesExpected = (elapsedUs * 192000) / 1000000;

        // Generate complete blocks only
        uint64_t blocksToGenerate = samplesExpected / BLOCK_SIZE;

        if (blocksToGenerate == 0) {
            return;
        }

        // Get oscillator parameters
        float freq = oscillatorSource->getFrequency();
        float amp = oscillatorSource->getAmplitude();

        // Convert amplitude - the oscillator amplitude is set as amp/500 in InitializeAudio
        double amplitude = amp * 500.0;
        // Convert frequency
        freq = freq*192000.0f/AUDIO_SAMPLE_RATE_EXACT;

        // Clamp amplitude to avoid int16 overflow
        if (amplitude > 32767.0) amplitude = 32767.0;

        // Noise amplitude is 1/20th of signal amplitude
        const double noiseAmp = amplitude / 40.0;

        // NCO increment per sample
        double phaseInc = OSC_TWO_PI * freq / OSC_SAMPLE_RATE;

        // Generate the blocks - both I and Q together
        for (uint64_t blk = 0; blk < blocksToGenerate; blk++) {
            // Check if buffer is full (use I read pointer as reference)
            uint32_t nextWriteBlock = (writeBlock + 1) % MAX_BLOCKS;
            if (nextWriteBlock == readBlockI || nextWriteBlock == readBlockQ) {
                // Buffer full, stop generating
                break;
            }

            // Generate one block of synchronized L/R samples
            int16_t* blockL = bufferI[writeBlock];
            int16_t* blockR = bufferQ[writeBlock];

            for (int i = 0; i < BLOCK_SIZE; i++) {
                double theta = phaseInc * sampleIndex;

                // L channel: nice clean tone)
                blockL[i] = (int16_t)(amplitude * cos(theta)+ noiseAmp * randomNoise());

                // R channel: same as L for mono microphone simulation
                blockR[i] = blockL[i];

                // Increment sample index (single increment for both I and Q)
                sampleIndex++;

                // Wrap at one second of samples to prevent floating point precision issues
                if (sampleIndex >= 192000) {
                    sampleIndex = 0;
                }
            }

            writeBlock = nextWriteBlock;
        }

        // Update timestamp
        lastGenerateTime += blocksToGenerate * (BLOCK_SIZE * 1000000 / 192000);
    }

    // Get number of blocks available for I channel
    int availableI() {
        generateSamples();
        uint32_t w = writeBlock;
        uint32_t r = readBlockI;
        if (w >= r) {
            return w - r;
        } else {
            return MAX_BLOCKS - r + w;
        }
    }

    // Get number of blocks available for Q channel
    int availableQ() {
        generateSamples();
        uint32_t w = writeBlock;
        uint32_t r = readBlockQ;
        if (w >= r) {
            return w - r;
        } else {
            return MAX_BLOCKS - r + w;
        }
    }

    // Read a block from I channel
    int16_t* readBufferI() {
        if (readBlockI == writeBlock) {
            // Buffer empty
            static int16_t emptyBuffer[BLOCK_SIZE] = {0};
            return emptyBuffer;
        }
        int16_t* ptr = bufferI[readBlockI];
        readBlockI = (readBlockI + 1) % MAX_BLOCKS;
        return ptr;
    }

    // Read a block from Q channel
    int16_t* readBufferQ() {
        if (readBlockQ == writeBlock) {
            // Buffer empty
            static int16_t emptyBuffer[BLOCK_SIZE] = {0};
            return emptyBuffer;
        }
        int16_t* ptr = bufferQ[readBlockQ];
        readBlockQ = (readBlockQ + 1) % MAX_BLOCKS;
        return ptr;
    }

private:
    AudioSynthWaveformSine* oscillatorSource;
    uint64_t sampleIndex;           // Single sample counter for both channels
    uint32_t writeBlock;            // Next block to write (shared for I and Q)
    uint32_t readBlockI;            // Next block to read for I channel
    uint32_t readBlockQ;            // Next block to read for Q channel
    uint64_t lastGenerateTime;      // Timestamp for timing
    bool enabled;

    // Separate buffers for I and Q channels
    int16_t bufferI[MAX_BLOCKS][BLOCK_SIZE];
    int16_t bufferQ[MAX_BLOCKS][BLOCK_SIZE];
};

// Global synchronized oscillator generator instance
static SynchronizedOscillatorGenerator g_syncOscillator;

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
    // If oscillator mode is enabled, use synchronized generator
    if (useOscillatorMode && g_syncOscillator.isEnabled()) {
        // Use channel to determine L or R
        // Channels 0 and 2 are left (L, L_Ex), channels 1 and 3 are right (R, R_Ex)
        if (channel == 0 || channel == 2) {
            return g_syncOscillator.availableI();
        } else {
            return g_syncOscillator.availableQ();
        }
    }

#ifdef USE_SDL_DISPLAY
    // For tone generators in simulator: pace based on output buffer level
    // Run DSP when output buffer needs data, sleep when buffer is full
    if (currentAudioSource == AUDIO_SOURCE_TWO_TONE ||
        currentAudioSource == AUDIO_SOURCE_SINGLE_TONE ||
        currentAudioSource == AUDIO_SOURCE_RXIQ_LSB ||
        currentAudioSource == AUDIO_SOURCE_RXIQ_USB) {
        size_t bufLevel = SDL_Audio_OutputBufferLevel();
        if (bufLevel > TARGET_BUFFER_LEVEL * 2) {
            // Buffer is well above target - sleep to let it drain
            // Sleep ~5ms (about 240 samples at 48kHz consumed)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return 0;
        }
        // Buffer needs data or is at reasonable level - run DSP
        return 17;
    }

    // For feedback mode: use direct feedback buffers
    // Q_out_L_Ex → g_feedbackL → Q_in_L (channel 0)
    // Q_out_R_Ex → g_feedbackR → Q_in_R (channel 1)
    if (currentAudioSource == AUDIO_SOURCE_FEEDBACK) {
        // Channels 0 and 1 read from feedback buffers
        if (channel == 0) {
            return g_feedbackL.available();
        } else if (channel == 1) {
            return g_feedbackR.available();
        }
        // Channels 2 and 3 (extended) use oscillator - handled above
        // Fall through to other sources if not handled
    }

    // For computer audio input: use actual sample availability
    if (currentAudioSource == AUDIO_SOURCE_COMPUTER && SDL_Audio_InputAvailable()) {
        // Still pace based on output buffer to prevent overrun
        if (!SDL_Audio_OutputNeedsData()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return 0;
        }
        size_t samplesAvailable = SDL_Audio_SamplesAvailable();
        return samplesAvailable / BUFFER_SIZE;
    }
#endif

    // Fallback to mock behavior (for unit tests without SDL)
    if (currentAudioSource == AUDIO_SOURCE_TWO_TONE ||
        currentAudioSource == AUDIO_SOURCE_SINGLE_TONE ||
        currentAudioSource == AUDIO_SOURCE_RXIQ_LSB ||
        currentAudioSource == AUDIO_SOURCE_RXIQ_USB) {
        size_t samplesAvailable = getToneSamplesAvailable();
        return samplesAvailable / BUFFER_SIZE;
    }

    // AUDIO_SOURCE_MOCK_DATA or fallback: use file-based mock data
    int blocks_available = 4*2048/BUFFER_SIZE;
    int answer = (blocks_available-head+1)*BUFFER_SIZE;
    if (answer >= 100) answer = 99;
    return answer;
}

void AudioRecordQueue::clear(void) {
    head = 0;
    // Also reset synchronized generator if in oscillator mode
    if (useOscillatorMode && g_syncOscillator.isEnabled()) {
        g_syncOscillator.clear();
    }
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
    // If oscillator mode is enabled, use synchronized generator
    if (useOscillatorMode && g_syncOscillator.isEnabled()) {
        // Use channel to determine L or R
        // Channels 0 and 2 are left (L, L_Ex), channels 1 and 3 are right (R, R_Ex)
        if (channel == 0 || channel == 2) {
            return g_syncOscillator.readBufferI();
        } else {
            return g_syncOscillator.readBufferQ();
        }
    }

    // Static buffers for generated I/Q samples
    static int16_t toneBuf_I[BUFFER_SIZE];
    static int16_t toneBuf_Q[BUFFER_SIZE];
    static bool qDataReady = false;

    // Handle tone generator sources
    if (currentAudioSource == AUDIO_SOURCE_TWO_TONE) {
        if (channel == 0 || channel == 2) {
            // I channel (Left) request - generate fresh data for both I and Q
            generateTwoToneSamples(toneBuf_I, toneBuf_Q, BUFFER_SIZE);
            qDataReady = true;
            return toneBuf_I;
        } else {
            // Q channel (Right) request - return cached Q data
            // Track samples consumed when Q is read (I+Q pair complete)
            toneSamplesConsumed += BUFFER_SIZE;
            if (qDataReady) {
                qDataReady = false;
                return toneBuf_Q;
            } else {
                // Q requested before I - generate fresh data
                generateTwoToneSamples(toneBuf_I, toneBuf_Q, BUFFER_SIZE);
                return toneBuf_Q;
            }
        }
    }

    if (currentAudioSource == AUDIO_SOURCE_RXIQ_LSB) {
        if (channel == 0 || channel == 2) {
            // I channel (Left) request - generate fresh data for both I and Q
            generateRXIQLSBSamples(toneBuf_I, toneBuf_Q, BUFFER_SIZE);
            qDataReady = true;
            return toneBuf_I;
        } else {
            // Q channel (Right) request - return cached Q data
            // Track samples consumed when Q is read (I+Q pair complete)
            toneSamplesConsumed += BUFFER_SIZE;
            if (qDataReady) {
                qDataReady = false;
                return toneBuf_Q;
            } else {
                // Q requested before I - generate fresh data
                generateRXIQLSBSamples(toneBuf_I, toneBuf_Q, BUFFER_SIZE);
                return toneBuf_Q;
            }
        }
    }

    if (currentAudioSource == AUDIO_SOURCE_RXIQ_USB) {
        if (channel == 0 || channel == 2) {
            // I channel (Left) request - generate fresh data for both I and Q
            generateRXIQUSBSamples(toneBuf_I, toneBuf_Q, BUFFER_SIZE);
            qDataReady = true;
            return toneBuf_I;
        } else {
            // Q channel (Right) request - return cached Q data
            // Track samples consumed when Q is read (I+Q pair complete)
            toneSamplesConsumed += BUFFER_SIZE;
            if (qDataReady) {
                qDataReady = false;
                return toneBuf_Q;
            } else {
                // Q requested before I - generate fresh data
                generateRXIQUSBSamples(toneBuf_I, toneBuf_Q, BUFFER_SIZE);
                return toneBuf_Q;
            }
        }
    }

    if (currentAudioSource == AUDIO_SOURCE_SINGLE_TONE) {
        if (channel == 0 || channel == 2) {
            // I channel (Left) request - generate fresh data for both I and Q
            generateSingleToneSamples(toneBuf_I, toneBuf_Q, BUFFER_SIZE);
            qDataReady = true;
            return toneBuf_I;
        } else {
            // Q channel (Right) request - return cached Q data
            // Track samples consumed when Q is read (I+Q pair complete)
            toneSamplesConsumed += BUFFER_SIZE;
            if (qDataReady) {
                qDataReady = false;
                return toneBuf_Q;
            } else {
                // Q requested before I - generate fresh data
                generateSingleToneSamples(toneBuf_I, toneBuf_Q, BUFFER_SIZE);
                return toneBuf_Q;
            }
        }
    }

    // For feedback mode: use direct feedback buffers
    // Q_out_L_Ex (ch 2) → g_feedbackL → Q_in_L (ch 0)
    // Q_out_R_Ex (ch 3) → g_feedbackR → Q_in_R (ch 1)
    if (currentAudioSource == AUDIO_SOURCE_FEEDBACK) {
        if (channel == 0) {
            return g_feedbackL.readBuffer();
        } else if (channel == 1) {
            return g_feedbackR.readBuffer();
        }
        // Channels 2 and 3 use oscillator mode (handled above)
    }

#ifdef USE_SDL_DISPLAY

    if (currentAudioSource == AUDIO_SOURCE_COMPUTER && SDL_Audio_InputAvailable()) {
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
    // Fallback to mock data (AUDIO_SOURCE_MOCK_DATA or fallback)
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

// ============== AudioRecordQueue Constructor ==============
AudioRecordQueue::AudioRecordQueue(void) :
    channel(0),
    enabled(0),
    head(0),
    data(nullptr),
    oscillatorSource(nullptr),
    oscillatorPhase(0.0),
    writeBlock(0),
    readBlock(0),
    lastGenerateTime(0),
    useOscillatorMode(false)
{
    // Initialize block buffer to zeros
    memset(blockBuffer, 0, sizeof(blockBuffer));
}

void AudioRecordQueue::setOscillatorSource(AudioSynthWaveformSine* osc) {
    useOscillatorMode = (osc != nullptr);

    // Configure the synchronized generator
    // Set up once when first channel enables (either I or Q can be first)
    if (osc != nullptr && !g_syncOscillator.isEnabled()) {
        g_syncOscillator.setSource(osc);
    } else if (osc == nullptr) {
        // If either channel disables, disable the generator
        g_syncOscillator.setSource(nullptr);
    }
}

void AudioRecordQueue::generateOscillatorSamples(void) {
    // This is now a no-op - generation is handled by the synchronized generator
    // when available() or readBuffer() is called
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

    // In feedback mode, apply frequency shift and write to feedback buffers
    // Q_out_L_Ex (channel 2) + Q_out_R_Ex (channel 3) → FreqShiftMFs4 → g_feedbackL/R
    if (currentAudioSource == AUDIO_SOURCE_FEEDBACK) {
        if (audioChannel == 2) {
            // Buffer I channel, wait for Q channel
            g_freqShiftFeedback.writeI(buf);
        } else if (audioChannel == 3) {
            // Combine with buffered I, apply freq shift, write to feedback buffers
            g_freqShiftFeedback.writeQ(buf);
        }
    }

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
