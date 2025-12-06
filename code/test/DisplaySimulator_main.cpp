/**
 * @file DisplaySimulator_main.cpp
 * @brief Main entry point for the SDL-based display simulator
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
 *   Space - Cycle through display states (Splash -> Home -> Menu)
 *   Up/Down arrows - Navigate menus
 *   Left/Right arrows - Adjust values
 */

#include <iostream>
#include <chrono>
#include <thread>

#ifdef USE_SDL_DISPLAY
#include <SDL2/SDL.h>
#endif

#include "SDT.h"
#include "MainBoard_Display.h"
#include "RA8875.h"
#include "Loop.h"
#include "UISm.h"
#include "ModeSm.h"
#include "PowerCalSm.h"

// External declarations
extern RA8875 tft;
extern UISm uiSM;
extern ModeSm modeSM;
extern PowerCalSm powerSM;

// Cleanup function declared in RA8875_SDL.cpp
#ifdef USE_SDL_DISPLAY
extern void RA8875_SDL_Cleanup();
#endif

// Simulated time is handled by Arduino_mock.cpp

// Simple keyboard event handler
enum SimulatorAction {
    ACTION_NONE,
    ACTION_QUIT,
    ACTION_CYCLE_STATE,
    ACTION_UP,
    ACTION_DOWN,
    ACTION_LEFT,
    ACTION_RIGHT,
    ACTION_ENTER
};

#ifdef USE_SDL_DISPLAY
SimulatorAction processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                return ACTION_QUIT;

            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        return ACTION_QUIT;
                    case SDLK_SPACE:
                        return ACTION_CYCLE_STATE;
                    case SDLK_UP:
                        return ACTION_UP;
                    case SDLK_DOWN:
                        return ACTION_DOWN;
                    case SDLK_LEFT:
                        return ACTION_LEFT;
                    case SDLK_RIGHT:
                        return ACTION_RIGHT;
                    case SDLK_RETURN:
                        return ACTION_ENTER;
                }
                break;
        }
    }
    return ACTION_NONE;
}
#endif

void printUsage() {
    std::cout << "\n=== Phoenix SDR Display Simulator ===" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  ESC          - Exit simulator" << std::endl;
    std::cout << "  SPACE        - Cycle display state (Splash -> Home -> Menu)" << std::endl;
    std::cout << "  UP/DOWN      - Navigate menus" << std::endl;
    std::cout << "  LEFT/RIGHT   - Adjust values" << std::endl;
    std::cout << "  ENTER        - Select/confirm" << std::endl;
    std::cout << "======================================\n" << std::endl;
}

int main(int argc, char* argv[]) {
#ifndef USE_SDL_DISPLAY
    std::cerr << "Error: This program requires SDL2 support." << std::endl;
    std::cerr << "Rebuild with: cmake -DUSE_SDL_DISPLAY=ON .." << std::endl;
    return 1;
#else
    printUsage();

    std::cout << "Initializing display..." << std::endl;

    // Initialize the display (this creates the SDL window)
    InitializeDisplay();

    std::cout << "Display initialized. Starting main loop..." << std::endl;

    // Initialize state machines to a known state
    UISm_ctor(&uiSM);
    UISm_start(&uiSM);

    ModeSm_ctor(&modeSM);
    ModeSm_start(&modeSM);

    PowerCalSm_ctor(&powerSM);
    PowerCalSm_start(&powerSM);

    // State machine starts in SPLASH state after start()
    // DO events will drive the transition to HOME after splashDuration_ms

    bool running = true;
    int frameCount = 0;
    auto lastFPSTime = std::chrono::steady_clock::now();

    while (running) {
        // Process keyboard/window events
        SimulatorAction action = processEvents();

        switch (action) {
            case ACTION_QUIT:
                running = false;
                break;

            case ACTION_CYCLE_STATE:
                // Cycle through states: SPLASH -> HOME -> MAIN_MENU -> HOME
                if (uiSM.state_id == UISm_StateId_SPLASH) {
                    std::cout << "Transitioning to HOME state" << std::endl;
                    UISm_dispatch_event(&uiSM, UISm_EventId_HOME);
                } else if (uiSM.state_id == UISm_StateId_HOME) {
                    std::cout << "Transitioning to MAIN_MENU state" << std::endl;
                    UISm_dispatch_event(&uiSM, UISm_EventId_MENU);
                } else if (uiSM.state_id == UISm_StateId_MAIN_MENU) {
                    std::cout << "Transitioning to SECONDARY_MENU state" << std::endl;
                    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
                } else if (uiSM.state_id == UISm_StateId_SECONDARY_MENU) {
                    std::cout << "Transitioning back to HOME state" << std::endl;
                    UISm_dispatch_event(&uiSM, UISm_EventId_HOME);
                } else {
                    std::cout << "Transitioning to HOME state" << std::endl;
                    UISm_dispatch_event(&uiSM, UISm_EventId_HOME);
                }
                break;

            case ACTION_UP:
                if (uiSM.state_id == UISm_StateId_MAIN_MENU) {
                    DecrementPrimaryMenu();
                } else if (uiSM.state_id == UISm_StateId_SECONDARY_MENU) {
                    DecrementSecondaryMenu();
                }
                break;

            case ACTION_DOWN:
                if (uiSM.state_id == UISm_StateId_MAIN_MENU) {
                    IncrementPrimaryMenu();
                } else if (uiSM.state_id == UISm_StateId_SECONDARY_MENU) {
                    IncrementSecondaryMenu();
                }
                break;

            case ACTION_LEFT:
                if (uiSM.state_id == UISm_StateId_SECONDARY_MENU) {
                    DecrementValue();
                }
                break;

            case ACTION_RIGHT:
                if (uiSM.state_id == UISm_StateId_SECONDARY_MENU) {
                    IncrementValue();
                }
                break;

            case ACTION_ENTER:
                if (uiSM.state_id == UISm_StateId_MAIN_MENU) {
                    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);
                } else if (uiSM.state_id == UISm_StateId_SECONDARY_MENU) {
                    UISm_dispatch_event(&uiSM, UISm_EventId_HOME);
                }
                break;

            default:
                break;
        }

        // Dispatch DO events to state machines (like PhoenixSketch.ino does)
        // This drives state machine behaviors and timed transitions
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
        UISm_dispatch_event(&uiSM, UISm_EventId_DO);
        PowerCalSm_dispatch_event(&powerSM, PowerCalSm_EventId_DO);

        // Update display
        DrawDisplay();

        // FPS counter
        frameCount++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastFPSTime).count();
        if (elapsed >= 5) {
            float fps = frameCount / (float)elapsed;
            std::cout << "FPS: " << fps << " | State: " << uiSM.state_id << std::endl;
            frameCount = 0;
            lastFPSTime = now;
        }

        // Small delay to prevent CPU spinning (target ~60 FPS)
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "Cleaning up..." << std::endl;
    RA8875_SDL_Cleanup();

    std::cout << "Simulator exited cleanly." << std::endl;
    return 0;
#endif
}
