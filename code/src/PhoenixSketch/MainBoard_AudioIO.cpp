/**
 * @file MainBoard_AudioIO.cpp
 * @brief Audio I/O management and routing for Phoenix SDR
 *
 * This module manages all audio input/output routing and configuration using the
 * OpenAudio library (a fork of the Teensy Audio Library). It handles the complex
 * switching between receive and transmit modes, ensuring proper signal paths for:
 * - Receive: ADC → DSP → Speaker
 * - Transmit SSB: Microphone → DSP → DAC → Exciter
 * - Transmit CW: Sidetone generator → Speaker (monitoring only)
 *
 * Hardware Architecture:
 * ----------------------
 * The Phoenix SDR uses two SGTL5000 audio codecs:
 *
 * 1. **Teensy Audio Board (sgtl5000_teensy)** - Transmit Path
 *    - Input: Microphone (for SSB voice transmission)
 *    - Output: I/Q signals to exciter board
 *    - Address: LOW
 *
 * 2. **Main Board (pcm5102_mainBoard)** - Receive Path
 *    - Input: I/Q signals from receiver (line-in from PCM1808)
 *    - Output: Demodulated audio to speaker via PCM5102
 *    - Address: HIGH
 *
 * Audio Signal Flow:
 * ------------------
 * The OpenAudio library uses a graph of interconnected audio processing blocks.
 * This module defines:
 * - **i2s_quadIn**: 4-channel input (mic L/R, RX I/Q)
 * - **i2s_quadOut**: 4-channel output (TX I/Q, speaker L/R)
 * - **Mixers**: Route signals between processing blocks based on radio mode
 * - **Queues**: Transfer audio samples between interrupt context and main loop
 * - **Sidetone**: Sine wave generator for CW monitoring
 *
 * Mode-Based Routing:
 * -------------------
 * The UpdateAudioIOState() function reconfigures the audio graph based on
 * the current ModeSm state:
 * - **SSB_RECEIVE/CW_RECEIVE**: RX I/Q → DSP → Speaker
 * - **SSB_TRANSMIT**: Microphone → DSP → TX I/Q
 * - **CW_TRANSMIT_*_MARK**: Sidetone → Speaker (no RF I/Q)
 *
 * Sample Rate Configuration:
 * --------------------------
 * The I2S interface supports multiple sample rates (48/96/192 kHz) configured
 * via PLL settings. SetI2SFreq() calculates and applies the required clock
 * divisors for the Teensy 4.1's audio subsystem.
 *
 * @see MainBoard_AudioIO.h for audio graph definitions
 * @see https://github.com/chipaudette/OpenAudio_ArduinoLibrary
 */

#include "MainBoard_AudioIO.h"

/**
 * The transition from analog to digital and digital to analog are handled using a fork
 * of the Teensy Audio Library: https://github.com/chipaudette/OpenAudio_ArduinoLibrary
 * 
 * i2s_quadIn is a quad channel audio input. It's channels are: 
 *  0: mic L from the Audio hat (mic for SSB)
 *  1: mic R from the Audio hat
 *  2: I/Q L from the PCM1808 (receiver IQ)        
 *  3: I/Q R from the PCM1808 (receiver IQ)        
 *
 * i2s_quadOut is a quad channel audio ouput. It's channels are: 
 *  0: L output for the Audio hat (exciter IQ)
 *  1: R output for the Audio hat (exciter IQ)
 *  2: L output for the speaker audio out
 *  3: R output for the speaker audio out
 * 
 * Each of these inputs and outputs go through a mixer that is used to turn them on/off. 
 * If you select channel 0 of the audio mixer, the signal passes through. If you select 
 * any other channel, then no signal is routed.
 * 
 * Microphone:
 *  Quad channels | 0                   | 1                   |
 *  Mixer name    | modeSelectInExL[0]  | modeSelectInExR[0]  |
 *  Record queue  | Q_in_L_Ex           | Q_in_R_Ex           |
 * 
 * Receive IQ:
 *  Quad channels | 2                   | 3                   |
 *  Mixer name    | modeSelectInL[0]    | modeSelectInR[0]    |
 *  Record queue  | Q_in_L              | Q_in_R              |
 * 
 * Speaker audio:
 *  Play queue    | Q_out_L             | Q_out_R             |
 *  Mixer name    | modeSelectOutL[0]   | modeSelectOutR[0]   |
 *  Quad channels | 2                   | 3                   |
 * 
 * Transmit IQ:
 *  Play queue    | Q_out_L_Ex          | Q_out_R_Ex          |
 *  Mixer name    | modeSelectOutExL[0] | modeSelectOutExR[0] |
 *  Quad channels | 0                   | 1                   |
 * 
 * The speaker audio also has a sidetone oscillator connected to port 2 of the mixers. 
 * The transmit IQ is also connected to port 1 of the output mixers, which allows you
 * to see what it is what you're trying to transmit. Probably don't need this anymore.
 */

// Generated using this tool: https://www.pjrc.com/teensy/gui/index.html
// GUItool: begin automatically generated code
AudioInputI2SQuad        i2s_quadIn;     //xy=288.75,387
AudioMixer4              modeSelectInR;  //xy=593.75,482
AudioMixer4              modeSelectInL;  //xy=596.75,389
AudioMixer4              modeSelectInExR; //xy=597.75,300
AudioMixer4              modeSelectInExL; //xy=598.75,194
AudioRecordQueue         Q_in_L_Ex;      //xy=789.75,198
AudioRecordQueue         Q_in_R_Ex;      //xy=792.75,301
AudioRecordQueue         Q_in_L;         //xy=797.75,394
AudioRecordQueue         Q_in_R;         //xy=798.75,483
AudioSynthWaveformSine   sidetone_oscillator; //xy=1087.75,485
AudioPlayQueue           Q_out_L_Ex;     //xy=1089.75,182
AudioPlayQueue           Q_out_R_Ex;     //xy=1090.75,240
AudioPlayQueue           Q_out_R;        //xy=1094.75,373
AudioPlayQueue           Q_out_L;        //xy=1096.75,302
AudioMixer4              modeSelectOutExL; //xy=1436.75,192
AudioMixer4              modeSelectOutL; //xy=1437.75,346
AudioMixer4              modeSelectOutExR; //xy=1439.75,265
AudioMixer4              modeSelectOutR; //xy=1444.75,453
AudioOutputI2SQuad       i2s_quadOut;    //xy=1681.75,300
AudioConnection          patchCord1(i2s_quadIn, 0, modeSelectInExL, 0);
AudioConnection          patchCord2(i2s_quadIn, 1, modeSelectInExR, 0);
AudioConnection          patchCord3(i2s_quadIn, 2, modeSelectInL, 0);
AudioConnection          patchCord4(i2s_quadIn, 3, modeSelectInR, 0);
AudioConnection          patchCord5(modeSelectInR, Q_in_R);
AudioConnection          patchCord6(modeSelectInL, Q_in_L);
AudioConnection          patchCord7(modeSelectInExR, Q_in_R_Ex);
AudioConnection          patchCord8(modeSelectInExL, Q_in_L_Ex);
AudioConnection          patchCord9(sidetone_oscillator, 0, modeSelectOutL, 2);
AudioConnection          patchCord10(sidetone_oscillator, 0, modeSelectOutR, 2);
AudioConnection          patchCord11(Q_out_L_Ex, 0, modeSelectOutExL, 0);
AudioConnection          patchCord12(Q_out_L_Ex, 0, modeSelectOutL, 1);
AudioConnection          patchCord13(Q_out_R_Ex, 0, modeSelectOutExR, 0);
AudioConnection          patchCord14(Q_out_R_Ex, 0, modeSelectOutR, 1);
AudioConnection          patchCord15(Q_out_R, 0, modeSelectOutR, 0);
AudioConnection          patchCord16(Q_out_L, 0, modeSelectOutL, 0);
AudioConnection          patchCord17(modeSelectOutExL, 0, i2s_quadOut, 0);
AudioConnection          patchCord18(modeSelectOutL, 0, i2s_quadOut, 2);
AudioConnection          patchCord19(modeSelectOutExR, 0, i2s_quadOut, 1);
AudioConnection          patchCord20(modeSelectOutR, 0, i2s_quadOut, 3);
AudioControlSGTL5000     pcm5102_mainBoard;     //xy=586.75,611
// GUItool: end automatically generated code

// Controller for the Teensy Audio Board. The web tool doesn't recognize the class
// type, so this variable is not included in the web tool's output.
AudioControlSGTL5000_Extended sgtl5000_teensy;

static ModeSm_StateId previousAudioIOState = ModeSm_StateId_ROOT;

/**
 * Get the previous audio I/O state.
 *
 * Returns the ModeSm state that the audio routing was last configured for.
 * Used to detect state changes and avoid unnecessary reconfiguration of the
 * audio graph when the mode hasn't changed.
 *
 * @return The previous ModeSm_StateId that audio routing was configured for
 */
ModeSm_StateId GetAudioPreviousState(void){
    return previousAudioIOState;
}

/**
 * Select a single active channel on a 4-channel audio mixer.
 *
 * This function implements a "one-hot" selection pattern where exactly one
 * input channel is enabled (gain=1.0) and all others are muted (gain=0.0).
 * Used to route signals through the audio graph by enabling/disabling mixer inputs.
 *
 * Example: To route RX I/Q through the audio DSP chain, select channel 0 on
 * the receive mixer. To route sidetone to speakers, select channel 2.
 *
 * @param mixer Pointer to the AudioMixer4 object to configure
 * @param channel Channel number to enable (0-3), all others will be muted
 */
void SelectMixerChannel(AudioMixer4 *mixer, uint8_t channel){
    for (uint8_t k = 0; k < 4; k++){
        if (k == channel) mixer->gain(k,1);
        else mixer->gain(k,0);
    }
}

/**
 * Mute all channels on a 4-channel audio mixer.
 *
 * Sets all four mixer channel gains to 0.0, effectively blocking all signal flow
 * through the mixer. Used during state transitions and when a particular signal
 * path needs to be completely disabled (e.g., muting TX output during receive).
 *
 * @param mixer Pointer to the AudioMixer4 object to mute
 */
void MuteMixerChannels(AudioMixer4 *mixer){
    for (uint8_t k = 0; k < 4; k++){
        mixer->gain(k,0);
    }
}

/**
 * Update the microphone gain for SSB transmission.
 *
 * Applies the current microphone gain setting (ED.currentMicGain) to the
 * SGTL5000 codec on the Teensy Audio Board. This controls the input amplification
 * for the microphone before SSB processing and modulation.
 *
 * Should be called when:
 * - User adjusts mic gain
 * - Transitioning to SSB transmit mode
 * - Restoring settings from storage
 */
void UpdateTransmitAudioGain(void){
    sgtl5000_teensy.micGain(ED.currentMicGain);
}

/**
 * Reconfigure audio I/O routing based on current radio mode state.
 *
 * This is the central audio routing function that responds to ModeSm state changes.
 * It reconfigures the entire audio graph (mixers and queues) to match the operational
 * requirements of each mode.
 *
 * Mode-Specific Configurations:
 * ------------------------------
 * **SSB_RECEIVE / CW_RECEIVE:**
 * - Start RX I/Q input queues (Q_in_L, Q_in_R)
 * - Stop microphone input queues
 * - Route RX I/Q (channels 2,3) → DSP processing
 * - Route DSP output → speaker (channels 0)
 * - Mute TX I/Q outputs and microphone inputs
 *
 * **SSB_TRANSMIT:**
 * - Start microphone input queues (Q_in_L_Ex, Q_in_R_Ex)
 * - Stop RX I/Q input queues
 * - Apply microphone gain setting
 * - Route microphone (channels 0,1) → DSP → TX I/Q output
 * - Mute speaker and RX I/Q inputs
 *
 * **CW_TRANSMIT_*_MARK (dit, dah, or straight key):**
 * - Stop all input queues (no mic, no RX I/Q)
 * - Route sidetone oscillator (channel 2) → speaker
 * - Mute all TX I/Q outputs (CW keying handled by RF board, not audio)
 * - Mute all other inputs/outputs
 *
 * **Other states (INIT, etc.):**
 * - Stop all input queues
 * - Mute all mixer channels (silence)
 *
 * Optimization:
 * -------------
 * Tracks previousAudioIOState to avoid redundant reconfiguration when the
 * state hasn't changed, minimizing audio glitches and CPU overhead.
 *
 * @note This function should be called from the main loop whenever a mode
 *       transition occurs, typically triggered by UpdateRFHardwareState()
 *
 * @see ModeSm state machine for state transition logic
 * @see UpdateRFHardwareState() in RFBoard.cpp
 */
void UpdateAudioIOState(void){
    if (modeSM.state_id == previousAudioIOState){
        // Already in this state, no need to change
        return;
    }
    switch (modeSM.state_id){
        case (ModeSm_StateId_CW_RECEIVE):
        case (ModeSm_StateId_SSB_RECEIVE):{
            // Microphone input stops
            Q_in_L_Ex.end();
            Q_in_R_Ex.end();
            // IQ from receive starts
            Q_in_L.begin();
            Q_in_R.begin();

            // Input is IQ samples from the receive board
            SelectMixerChannel(&modeSelectInL, 0);
            SelectMixerChannel(&modeSelectInR, 0);
            // Output is audio playing on the speaker coming from the receive DSP chain
            SelectMixerChannel(&modeSelectOutL, 0);
            SelectMixerChannel(&modeSelectOutR, 0);
            // No input is being received from microphone
            MuteMixerChannels(&modeSelectInExL);
            MuteMixerChannels(&modeSelectInExR);
            // And no output is being send to RF transmit
            MuteMixerChannels(&modeSelectOutExL);
            MuteMixerChannels(&modeSelectOutExR);
            break;
        }
        case (ModeSm_StateId_SSB_TRANSMIT):{
            // IQ from receive stops
            Q_in_L.end(); 
            Q_in_R.end();
            // Microphone input starts
            Q_in_L_Ex.begin(); 
            Q_in_R_Ex.begin();
            sgtl5000_teensy.micGain(ED.currentMicGain);

            // Input is microphone
            SelectMixerChannel(&modeSelectInExL,0);
            SelectMixerChannel(&modeSelectInExR,0);
            // Output is samples to RF transmit
            SelectMixerChannel(&modeSelectOutExL,0);
            SelectMixerChannel(&modeSelectOutExR,0);
            // Mute IQ samples from the receive board
            MuteMixerChannels(&modeSelectInL);
            MuteMixerChannels(&modeSelectInR);
            // Mute speaker audio
            MuteMixerChannels(&modeSelectOutL);
            MuteMixerChannels(&modeSelectOutR);
            break;
        }            
        case (ModeSm_StateId_CW_TRANSMIT_MARK):
        case (ModeSm_StateId_CW_TRANSMIT_DIT_MARK):
        case (ModeSm_StateId_CW_TRANSMIT_DAH_MARK):
            // IQ from receive stops
            Q_in_L.end(); 
            Q_in_R.end();
            // Microphone input stops
            Q_in_L_Ex.end(); 
            Q_in_R_Ex.end();

            // We need to play the sidetone audio on the speaker, others muted
            SelectMixerChannel(&modeSelectOutL, 2); // sidetone
            SelectMixerChannel(&modeSelectOutR, 2); // sidetone
            // Mute IQ samples from the receive board
            MuteMixerChannels(&modeSelectInL);
            MuteMixerChannels(&modeSelectInR);
            // No output is being send to RF transmit
            MuteMixerChannels(&modeSelectOutExL);
            MuteMixerChannels(&modeSelectOutExR);
            // No input is being received from microphone
            MuteMixerChannels(&modeSelectInExL);
            MuteMixerChannels(&modeSelectInExR);
            break;

        default:{
            // IQ from receive stops
            Q_in_L.end(); 
            Q_in_R.end();
            // Microphone input stops
            Q_in_L_Ex.end(); 
            Q_in_R_Ex.end();
            // Mute all channels
            MuteMixerChannels(&modeSelectInL);
            MuteMixerChannels(&modeSelectInR);
            MuteMixerChannels(&modeSelectOutL);
            MuteMixerChannels(&modeSelectOutR);
            MuteMixerChannels(&modeSelectInExL);
            MuteMixerChannels(&modeSelectInExR);
            MuteMixerChannels(&modeSelectOutExL);
            MuteMixerChannels(&modeSelectOutExR);
            break;
        }
    }
    previousAudioIOState = modeSM.state_id;
}

/**
 * Initialize all audio subsystems and configure hardware codecs.
 *
 * Performs complete initialization sequence for the dual-codec audio architecture:
 *
 * Initialization Steps:
 * ---------------------
 * 1. **I2S Clock Configuration**
 *    - Sets I2S sample rate via PLL (48/96/192 kHz from SR[SampleRate].rate)
 *    - Configures both SAI1 and SAI2 peripherals
 *
 * 2. **Teensy Audio Board Codec (sgtl5000_teensy)** - Transmit Path
 *    - Address: LOW
 *    - Input: Microphone with 10 dB initial gain
 *    - Output: Line-out level 13 (I/Q to exciter)
 *    - Disables ADC high-pass filter (reduces noise per PJRC forum recommendation)
 *
 * 3. **Main Board Codec (pcm5102_mainBoard)** - Receive Path
 *    - Address: HIGH
 *    - Input: Line-in from RX I/Q (PCM1808)
 *    - Output: Speaker at 50% volume
 *
 * 4. **Audio Memory Allocation**
 *    - 500 blocks for regular audio (int16 samples)
 *    - 10 blocks for floating-point audio (F32)
 *
 * 5. **Sidetone Generator**
 *    - Frequency: SIDETONE_FREQUENCY (typically 600-800 Hz)
 *    - Amplitude: ED.sidetoneVolume / 500 (normalized 0.0-1.0)
 *    - Initially muted to prevent startup tone
 *
 * This function must be called during radio initialization before entering the
 * main loop, after hardware power-up but before audio processing begins.
 *
 * @note Memory allocation sizes (500/10) are sized for the maximum expected
 *       processing depth at the highest sample rate
 *
 * @see SetI2SFreq() for sample rate configuration details
 * @see SR[] array in Config.h for supported sample rates
 */
void InitializeAudio(void){
    SetI2SFreq(SR[SampleRate].rate);
    // The sgtl5000_teensy is the controller for the Teensy Audio board. We use it to get 
    // the microphone input for SSB, and the I/Q output for the exciter board. In other
    // words, it is used for the transmit path.
    sgtl5000_teensy.setAddress(LOW);
    sgtl5000_teensy.enable();
    AudioMemory(500);
    AudioMemory_F32(10);
    sgtl5000_teensy.inputSelect(AUDIO_INPUT_MIC);
    sgtl5000_teensy.micGain(10);
    sgtl5000_teensy.lineInLevel(0);
    sgtl5000_teensy.lineOutLevel(13);
    //reduces noise.  https://forum.pjrc.com/threads/27215-24-bit-audio-boards?p=78831&viewfull=1#post78831
    sgtl5000_teensy.adcHighPassFilterDisable();

    // The sgtl5000_mainBoard is the controller for the audio inputs and outputs on the 
    // main board. We use it to digitize the IQ outputs of the receive chain and to produce
    // the audio outputs to the speaker.
    pcm5102_mainBoard.setAddress(HIGH);
    pcm5102_mainBoard.enable();
    pcm5102_mainBoard.inputSelect(AUDIO_INPUT_LINEIN);
    pcm5102_mainBoard.volume(0.5);

    // Mute the sidetone channel now otherwise we get a short tone on radio startup
    // before the state machine mutes it
    MuteMixerChannels(&modeSelectOutL); // sidetone
    MuteMixerChannels(&modeSelectOutR); // sidetone
    sidetone_oscillator.amplitude(ED.sidetoneVolume / 500);
    sidetone_oscillator.frequency(SIDETONE_FREQUENCY);

}

/**
 * Set the I2S sample frequency
 * 
 * @param freq The frequency to set, units of Hz
 * @return The frequency or 0 if too large
 */
int SetI2SFreq(int freq) {
    int n1;
    int n2;
    int c0;
    int c2;
    int c1;
    double C;

    // PLL between 27*24 = 648MHz und 54*24=1296MHz
    // Fudge to handle 8kHz - El Supremo
    if (freq > 8000) {
        n1 = 4;  //SAI prescaler 4 => (n1*n2) = multiple of 4
    } else {
        n1 = 8;
    }
    n2 = 1 + (24000000 * 27) / (freq * 256 * n1);
    if (n2 > 63) {
        // n2 must fit into a 6-bit field
    #if defined(DEBUG)
        Serial.printf("ERROR: n2 exceeds 63 - %d\n", n2);
    #endif
        return 0;
    }
    C = ((double)freq * 256 * n1 * n2) / 24000000;
    c0 = C;
    c2 = 10000;
    c1 = C * c2 - (c0 * c2);
    set_audioClock(c0, c1, c2, true);
    CCM_CS1CDR = (CCM_CS1CDR & ~(CCM_CS1CDR_SAI1_CLK_PRED_MASK | CCM_CS1CDR_SAI1_CLK_PODF_MASK))
                | CCM_CS1CDR_SAI1_CLK_PRED(n1 - 1)   // &0x07
                | CCM_CS1CDR_SAI1_CLK_PODF(n2 - 1);  // &0x3f

    CCM_CS2CDR = (CCM_CS2CDR & ~(CCM_CS2CDR_SAI2_CLK_PRED_MASK | CCM_CS2CDR_SAI2_CLK_PODF_MASK))
                | CCM_CS2CDR_SAI2_CLK_PRED(n1 - 1)   // &0x07
                | CCM_CS2CDR_SAI2_CLK_PODF(n2 - 1);  // &0x3f)
    return freq;
}
