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

ModeSm_StateId previousState = ModeSm_StateId_ROOT;

ModeSm_StateId GetAudioPreviousState(void){
    return previousState;
}

void SelectMixerChannel(AudioMixer4 *mixer, uint8_t channel){
    for (uint8_t k = 0; k < 4; k++){
        if (k == channel) mixer->gain(k,1);
        else mixer->gain(k,0);
    }
}

void MuteMixerChannels(AudioMixer4 *mixer){
    for (uint8_t k = 0; k < 4; k++){
        mixer->gain(k,0);
    }
}

void UpdateAudioIOState(void){
    if (modeSM.state_id == previousState){
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
            sgtl5000_teensy.micGain(EEPROMData.currentMicGain);

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
    previousState = modeSM.state_id;
}

/**
 * Perform setup of the audio input and output
 */
void SetupAudio(void){
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
