/**
 * @file RadioSimulator_main.cpp
 * @brief Main entry point for the SDL-based radio simulator
 *
 * This program creates an 800x480 window that simulates the RA8875 TFT display
 * used by the Phoenix SDR radio. It allows testing and development of the
 * display code on a desktop computer without the actual hardware.
 *
 * Build with: cmake -DUSE_SDL_DISPLAY=ON .. && make display_simulator
 * Run with: ./display_simulator
 *
 * Controls:
 *   ESC or close window - Exit
 *
 *   ---- Front Panel Buttons (top row: 1-9, bottom row: q-h) ----
 *   1 - Menu Select (button 0)    q - Noise Reduction (button 9)
 *   2 - Main Menu (button 1)      w - Notch Filter (button 10)
 *   3 - Band Up (button 2)        e - Fine Tune Inc (button 11)
 *   4 - Zoom (button 3)           r - Filter (button 12)
 *   5 - Reset Tuning (button 4)   t - Decoder Toggle (button 13)
 *   6 - Band Down (button 5)      y - DFE (button 14)
 *   7 - Toggle Mode (button 6)    u - Bearing (button 15)
 *   8 - Demodulation (button 7)   o - Spare (button 16)
 *   9 - Main Tune Inc (button 8)  h - Home Screen (button 17)
 *
 *   ---- Additional Buttons ----
 *   v - Volume Button (button 18)
 *   f - Filter Button (button 19)
 *   n - Fine Tune Button (button 20)
 *   b - VFO Toggle (button 21)
 *
 *   ---- Encoders ----
 *   Up/Down arrows    - Filter encoder (menu navigation / filter adjust)
 *   Left/Right arrows - Value adjust in menus
 *   +/- (numpad)      - Volume encoder
 *   [/]               - Main tune encoder
 *   ;/'               - Fine tune encoder
 *
 *   ---- PTT and CW Keys ----
 *   p - PTT (toggle to transmit SSB)
 *   . - CW Key 1 (straight key / dit)
 *   / - CW Key 2 (dah for iambic keyer)
 *
 *   ---- Audio Source ----
 *   a - Cycle audio input source (Computer/Two-Tone/Single-Tone/RXIQ/Feedback)
 */


#include <iostream>
#include <chrono>
#include <atomic>
#include <thread>

#ifdef USE_SDL_DISPLAY
#include <SDL2/SDL.h>
#endif

// Timer variables for interrupt simulation
static std::atomic<bool> timer_running{false};
static std::thread timer_thread;

#include "SDT.h"
#include "MainBoard_Display.h"
#include "RA8875.h"
#include "Loop.h"
#include "UISm.h"
#include "ModeSm.h"
#include "PowerCalSm.h"
#include "FrontPanel.h"
#include "Config.h"
#include "OpenAudio_ArduinoLibrary.h"
#include "LittleFS_mock.h"
#include "HardwareSm.h"

// External declarations
extern RA8875 tft;
extern UISm uiSM;
extern ModeSm modeSM;
extern PowerCalSm powerSM;
extern LittleFS_Program myfs;  // From Storage.cpp
void setup(void); // forward declaration of setup function

// External function from Arduino_mock.cpp to start the millis() timer
extern void StartMillis(void);

// Cleanup function declared in RA8875_SDL.cpp
#ifdef USE_SDL_DISPLAY
extern void RA8875_SDL_Cleanup();
#endif

// Flag to signal shutdown was requested
static bool shutdownRequested = false;

// Track the previous RF hardware state to detect changes
static RFHardwareState previousRFState = RFInvalid;
static int32_t oldband = -1;

/**
 * Update audio input source based on RF hardware state.
 * Automatically switches to appropriate audio sources for calibration modes:
 * - RFCalReceiveIQ: Use RXIQ_LSB for USB mode, RXIQ_USB for LSB mode
 * - RFCalTransmitIQ: Use Feedback mode to loop TX audio back as RX input
 */
static void updateAudioSourceForRFState() {
    RFHardwareState currentState = GetRFHardwareState();

    // Only update when state changes
    if ((currentState == previousRFState) && (ED.currentBand[ED.activeVFO] == oldband)) {
        return;
    }
    oldband = ED.currentBand[ED.activeVFO];
    switch (currentState) {
        case RFCalReceiveIQ:
            // For RX IQ calibration, use the opposite sideband tone source
            // USB mode needs LSB tone, LSB mode needs USB tone
            if (bands[ED.currentBand[ED.activeVFO]].mode == USB){
                setAudioInputSource(AUDIO_SOURCE_RXIQ_LSB);
                std::cout << "Audio Source (auto): RX IQ tones (LSB) for USB calibration" << std::endl;
            } else {
                setAudioInputSource(AUDIO_SOURCE_RXIQ_USB);
                std::cout << "Audio Source (auto): RX IQ tones (USB) for LSB calibration" << std::endl;
            }
            break;

        case RFCalTransmitIQ:
            // For TX IQ calibration, use feedback mode to loop TX back to RX
            setAudioInputSource(AUDIO_SOURCE_FEEDBACK);
            std::cout << "Audio Source (auto): Feedback for TX IQ calibration" << std::endl;
            break;

        default:
            // For other states, don't automatically change the audio source
            break;
    }

    previousRFState = currentState;
}

// Simulated time is handled by Arduino_mock.cpp

// Simple keyboard event handler
enum SimulatorAction {
    ACTION_NONE,
    ACTION_QUIT
};

// Track key states for PTT and CW keys (need to detect release)
static bool pttKeyDown = false;
static bool pttEngaged = false;  // Track latching PTT state
static bool cwKey1Down = false;

#ifdef USE_SDL_DISPLAY
/**
 * Helper to simulate a button press.
 * Sets the button ID and queues a button press interrupt.
 */
static void simulateButtonPress(int buttonId) {
    SetButton(buttonId);
    SetInterrupt(iBUTTON_PRESSED);
}

/**
 * Process SDL keyboard and window events.
 * Maps keyboard keys to radio button presses and encoder rotations.
 * Returns ACTION_QUIT when the user wants to exit.
 */
SimulatorAction processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                // Window closed - do proper shutdown
                ShutdownTeensy();
                shutdownRequested = true;
                return ACTION_QUIT;

            case SDL_KEYDOWN:
                // Skip auto-repeat events
                if (event.key.repeat) break;

                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        // Request shutdown via the normal shutdown routine
                        ShutdownTeensy();
                        shutdownRequested = true;
                        return ACTION_QUIT;

                    // ---- Front Panel Buttons (top row: 1-9) ----
                    case SDLK_1: simulateButtonPress(MENU_OPTION_SELECT); break;
                    case SDLK_2: simulateButtonPress(MAIN_MENU_UP); break;
                    case SDLK_3: simulateButtonPress(BAND_UP); break;
                    case SDLK_4: simulateButtonPress(ZOOM); break;
                    case SDLK_5: simulateButtonPress(RESET_TUNING); break;
                    case SDLK_6: simulateButtonPress(BAND_DN); break;
                    case SDLK_7: simulateButtonPress(TOGGLE_MODE); break;
                    case SDLK_8: simulateButtonPress(DEMODULATION); break;
                    case SDLK_9: simulateButtonPress(MAIN_TUNE_INCREMENT); break;

                    // ---- Front Panel Buttons (bottom row: q-o) ----
                    case SDLK_q: simulateButtonPress(NOISE_REDUCTION); break;
                    case SDLK_w: simulateButtonPress(NOTCH_FILTER); break;
                    case SDLK_e: simulateButtonPress(FINE_TUNE_INCREMENT); break;
                    case SDLK_r: simulateButtonPress(FILTER); break;
                    case SDLK_t: simulateButtonPress(DECODER_TOGGLE); break;
                    case SDLK_y: simulateButtonPress(DFE); break;
                    case SDLK_u: simulateButtonPress(BEARING); break;
                    case SDLK_o: simulateButtonPress(SPARE); break;
                    case SDLK_h: simulateButtonPress(HOME_SCREEN); break;

                    // ---- Additional Buttons ----
                    case SDLK_v: simulateButtonPress(VOLUME_BUTTON); break;
                    case SDLK_f: simulateButtonPress(FILTER_BUTTON); break;
                    case SDLK_n: simulateButtonPress(FINETUNE_BUTTON); break;
                    case SDLK_b: simulateButtonPress(VFO_TOGGLE); break;

                    // ---- Encoders: Filter (Up/Down arrows) ----
                    case SDLK_UP: SetInterrupt(iFILTER_DECREASE); break;
                    case SDLK_DOWN: SetInterrupt(iFILTER_INCREASE); break;

                    // ---- Encoders: Volume (+/- on numpad or regular) ----
                    case SDLK_KP_PLUS:
                    case SDLK_EQUALS: // + on regular keyboard (shift+equals)
                        SetInterrupt(iVOLUME_INCREASE); break;
                    case SDLK_KP_MINUS:
                    case SDLK_MINUS:
                        SetInterrupt(iVOLUME_DECREASE); break;

                    // ---- Encoders: Main Tune ([/]) ----
                    case SDLK_RIGHTBRACKET: SetInterrupt(iCENTERTUNE_INCREASE); break;
                    case SDLK_LEFTBRACKET: SetInterrupt(iCENTERTUNE_DECREASE); break;

                    // ---- Encoders: Fine Tune (;/') ----
                    case SDLK_QUOTE: SetInterrupt(iFINETUNE_INCREASE); break;
                    case SDLK_SEMICOLON: SetInterrupt(iFINETUNE_DECREASE); break;

                    // ---- Left/Right arrows for value adjustment in menus ----
                    case SDLK_LEFT: SetInterrupt(iFILTER_DECREASE); break;
                    case SDLK_RIGHT: SetInterrupt(iFILTER_INCREASE); break;

                    // ---- PTT (Toggle to transmit SSB) ----
                    case SDLK_p:
                        if (!pttKeyDown) {
                            pttKeyDown = true;
                            pttEngaged = !pttEngaged;
                            if (pttEngaged) {
                                SetInterrupt(iPTT_PRESSED);
                                std::cout << "PTT ENGAGED" << std::endl;
                            } else {
                                SetInterrupt(iPTT_RELEASED);
                                std::cout << "PTT DISENGAGED" << std::endl;
                            }
                        }
                        break;

                    // ---- CW Key 1 (straight key or dit) ----
                    case SDLK_PERIOD:
                        if (!cwKey1Down) {
                            cwKey1Down = true;
                            SetInterrupt(iKEY1_PRESSED);
                            std::cout << "KEY1 PRESSED" << std::endl;
                        }
                        break;

                    // ---- CW Key 2 (dah for iambic keyer) ----
                    case SDLK_SLASH:
                        SetInterrupt(iKEY2_PRESSED);
                        std::cout << "KEY2 PRESSED" << std::endl;
                        break;

                    // ---- Audio Source Selection ----
                    case SDLK_a: {
                        // Cycle through audio sources
                        AudioInputSource current = getAudioInputSource();
                        AudioInputSource next;
                        switch (current) {
                            case AUDIO_SOURCE_COMPUTER:
                                next = AUDIO_SOURCE_TWO_TONE;
                                break;
                            case AUDIO_SOURCE_TWO_TONE:
                                next = AUDIO_SOURCE_SINGLE_TONE;
                                break;
                            case AUDIO_SOURCE_SINGLE_TONE:
                                next = AUDIO_SOURCE_RXIQ_LSB;
                                break;
                            case AUDIO_SOURCE_RXIQ_LSB:
                                next = AUDIO_SOURCE_RXIQ_USB;
                                break;
                            case AUDIO_SOURCE_RXIQ_USB:
                                next = AUDIO_SOURCE_FEEDBACK;
                                break;
                            case AUDIO_SOURCE_FEEDBACK:
                            default:
                                next = AUDIO_SOURCE_COMPUTER;
                                break;
                        }
                        setAudioInputSource(next);
                        std::cout << "Audio Source: " << getAudioInputSourceName() << std::endl;
                        break;
                    }
                }
                break;

            case SDL_KEYUP:
                switch (event.key.keysym.sym) {
                    // ---- PTT Key Release (just clear key state, don't change PTT) ----
                    case SDLK_p:
                        pttKeyDown = false;
                        break;

                    // ---- CW Key 1 Release ----
                    case SDLK_PERIOD:
                        if (cwKey1Down) {
                            cwKey1Down = false;
                            SetInterrupt(iKEY1_RELEASED);
                            std::cout << "KEY1 RELEASED" << std::endl;
                        }
                        break;
                }
                break;
        }
    }
    return ACTION_NONE;
}
#endif

void printUsage() {
    std::cout << "\n=== Phoenix SDR Radio Simulator ===" << std::endl;
    std::cout << "This simulator runs the full radio loop() function," << std::endl;
    std::cout << "allowing you to interact with the radio via keyboard." << std::endl;
    std::cout << "\nControls:" << std::endl;
    std::cout << "  ESC           - Exit simulator" << std::endl;
    std::cout << "\n  --- Front Panel Buttons ---" << std::endl;
    std::cout << "  1-8           - Top row buttons (Select, Menu, Band+, Zoom, Reset, Band-, Mode, Demod)" << std::endl;
    std::cout << "  q-i           - Bottom row buttons (NR, Notch, FT Inc, Filter, Decode, DFE, Bearing, Tune Inc)" << std::endl;
    std::cout << "  h             - Home screen" << std::endl;
    std::cout << "  v, f, n, b    - Volume, Filter, FineTune buttons, VFO toggle" << std::endl;
    std::cout << "\n  --- Encoders ---" << std::endl;
    std::cout << "  Up/Down       - Filter encoder (menu nav / filter adjust)" << std::endl;
    std::cout << "  Left/Right    - Also filter encoder (for value adjustment)" << std::endl;
    std::cout << "  +/-           - Volume encoder" << std::endl;
    std::cout << "  [ / ]         - Main tune encoder" << std::endl;
    std::cout << "  ; / '         - Fine tune encoder" << std::endl;
    std::cout << "\n  --- PTT and CW ---" << std::endl;
    std::cout << "  p (toggle)    - PTT (transmit SSB)" << std::endl;
    std::cout << "  . (hold)      - CW Key 1 (straight key / dit)" << std::endl;
    std::cout << "  /             - CW Key 2 (dah)" << std::endl;
    std::cout << "\n  --- Audio Source ---" << std::endl;
    std::cout << "  a             - Cycle audio source (Computer/Two-Tone/Single-Tone/RXIQ/Feedback)" << std::endl;
    std::cout << "============================================\n" << std::endl;
}

/**
 * Timer interrupt function that runs every 1ms
 * Dispatches DO events to the state machines
 */
void timer1ms(void) {
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    UISm_dispatch_event(&uiSM, UISm_EventId_DO);
    PowerCalSm_dispatch_event(&powerSM, PowerCalSm_EventId_DO);
    ReceiveIQCalSm_dispatch_event(&rxiqSM, ReceiveIQCalSm_EventId_DO);
}

/**
 * Start the 1ms timer interrupt
 */
void start_timer1ms() {
    if (timer_running.load()) return; // Already running

    timer_running.store(true);
    timer_thread = std::thread([]() {
        while (timer_running.load()) {
            timer1ms();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
}

/**
 * Stop the 1ms timer interrupt
 */
void stop_timer1ms() {
    if (!timer_running.load()) return; // Already stopped

    timer_running.store(false);
    if (timer_thread.joinable()) {
        timer_thread.join();
    }
}


int main(int argc, char* argv[]) {
#ifndef USE_SDL_DISPLAY
    std::cerr << "Error: This program requires SDL2 support." << std::endl;
    std::cerr << "Rebuild with: cmake -DUSE_SDL_DISPLAY=ON .." << std::endl;
    return 1;
#else
    printUsage();

    // Set up the queues so we get some simulated data through and start the "clock"
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
    Q_in_L_Ex.setChannel(0);
    Q_in_R_Ex.setChannel(1);
    Q_in_L_Ex.clear();
    Q_in_R_Ex.clear();

    // Set up audio output queues with channel assignment
    // Channels 0/1: Audio output (speaker playback, downsampled to 48kHz)
    // Channels 2/3: IQ output (for feedback loopback, full 192kHz)
    Q_out_L.setAudioChannel(0);    // Left audio channel
    Q_out_R.setAudioChannel(1);    // Right audio channel
    Q_out_L_Ex.setAudioChannel(2); // I channel (IQ output)
    Q_out_R_Ex.setAudioChannel(3); // Q channel (IQ output)

    // Initialize SDL audio for demodulated audio playback
    // Using 192kHz to match the DSP output sample rate (SR[SampleRate].rate)
    // Most modern audio systems can handle this via software resampling
    if (!SDL_Audio_Init(192000)) {
        std::cerr << "Warning: SDL Audio initialization failed. Audio playback disabled." << std::endl;
    } else {
        std::cout << "SDL Audio initialized for demodulated audio playback" << std::endl;
    }

    // Initialize the millis() timer for timing functions
    StartMillis();

    // Set up disk backing for LittleFS so config persists between sessions
    // The config file will be saved in the current working directory
    myfs.setDiskBackingPath(".");

    std::cout << "Initializing display..." << std::endl;


    setup();

    /*InitializeFrontPanel();
    InitializeSignalProcessing();
    InitializeAudio();
    InitializeDisplay();
    InitializeRFHardware(); // RF board, LPF board, and BPF board

    std::cout << "Display initialized. Starting main loop..." << std::endl;

    // Initialize state machines to a known state
    UISm_ctor(&uiSM);
    UISm_start(&uiSM);
    UpdateAudioIOState();

    ModeSm_ctor(&modeSM);
    ModeSm_start(&modeSM);

    PowerCalSm_ctor(&powerSM);
    PowerCalSm_start(&powerSM);
    */

    // Initialize key pins to released state (active low)
    digitalWrite(KEY1, 1); // KEY1 released
    digitalWrite(KEY2, 1); // KEY2 released
    
    // Now, start the 1ms timer interrupt to simulate hardware timer
    start_timer1ms();

    // State machine starts in SPLASH state after start()
    // DO events will drive the transition to HOME after splashDuration_ms

    bool running = true;
    int frameCount = 0;
    auto lastFPSTime = std::chrono::steady_clock::now();

    std::cout << "Running simulator - press ESC to exit, or use keyboard to control radio" << std::endl;


    while (running) {
        // Process keyboard/window events and map to radio interrupts
        SimulatorAction action = processEvents();

        if (action == ACTION_QUIT) {
            running = false;
            break;
        }

        // Run the main radio loop
        // This processes interrupts, dispatches to state machines, and updates the display
        loop();

        // Update audio source based on RF hardware state (for calibration modes)
        updateAudioSourceForRFState();

        // Update the SDL display once per frame (not on every draw call)
        tft.updateScreen();

        // FPS counter
        /*frameCount++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastFPSTime).count();
        if (elapsed >= 5) {
            float fps = frameCount / (float)elapsed;
            std::cout << "FPS: " << fps
                      << " | UI State: " << uiSM.state_id
                      << " | Mode State: " << modeSM.state_id
                      << std::endl;
            frameCount = 0;
            lastFPSTime = now;
        }*/

        // Small delay to prevent CPU spinning (target ~60 FPS)
        //std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "Cleaning up..." << std::endl;
    stop_timer1ms();  // Stop the timer thread before cleanup

    // Sync LittleFS storage to disk (config was saved by ShutdownTeensy)
    myfs.syncToDisk();

    SDL_Audio_Cleanup();  // Clean up audio before display
    RA8875_SDL_Cleanup();

    std::cout << "Simulator exited cleanly." << std::endl;
    return 0;
#endif
}
