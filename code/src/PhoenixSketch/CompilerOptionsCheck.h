#pragma once
//
//
// Copy the CompilerOptionsCheck.h file ( pasted below ) into the code/src/PhoenixSketch/   directory 
// Edit the code/src/PhoenixSketch/PhoenixSketch.ino
//	and the line 
//				#include "CompilerOptionsCheck.h"
// at the beginning of the PhoenixSketch.ino file
//
//
// Edit the CompilerOptionsCheck.h
//
//	Line 11 
//		#define REQUIRED_USB_MODE 3   // <-- change to 1, 2, or 3
//									1    // For Dual Serial 				(Tools > USB Type > Serial + Serial)
//									2   // For Serial + MIDI + Audio 		(Tools > USB Type > Serial + MIDI + Audio)
//									3   // For Triple Serial 				(Tools > USB Type > Serial + Serial)
//
//	Line 22
//		#if (F_CPU == 528000000)		//  528mhz =  528000000 		600mhz =   600000000
//
//
// ================= Select required USB Type =================
// Choose ONE of these by setting REQUIRED_USB_MODE:
//
//  1 = Dual Serial              (Tools > USB Type > Serial + Serial)
//  2 = Serial + MIDI + Audio    (Tools > USB Type > Serial + MIDI + Audio)
//  3 = Triple Serial            (Tools > USB Type > Triple Serial)
// ============================================================

#define REQUIRED_USB_MODE 1   // <-- change to 1, 2, or 3


// ---------- Board OK? ----------
#if defined(TEENSY41) || defined(ARDUINO_TEENSY41)
  #define BOARD_OK 1
#else
  #define BOARD_OK 0
#endif

// ---------- CPU OK? ----------
#if (F_CPU == 600000000)
  #define CPU_OK 1
#else
  #define CPU_OK 0
#endif

// ---------- Optimization OK? ----------
// Tools > Optimize > Smallest Code with LTO sets the compiler flags:
//     -Os -flto -fno-fat-lto-objects
// GCC defines __OPTIMIZE_SIZE__ when -Os is active, so we can verify the size
// half reliably. There is no standard preprocessor macro for -flto, so the LTO
// half cannot be verified at preprocess time — the user still has to pick the
// "...with LTO" variant manually in the IDE. This check catches the most common
// mistake: someone building with -O2 / Faster by accident.
    // #if defined(__OPTIMIZE_SIZE__)
    //   #define OPT_OK 1
    // #else
    //   #define OPT_OK 0
    // #endif


// ---------- USB OK? (depends on REQUIRED_USB_MODE) ----------
#if (REQUIRED_USB_MODE == 1)
// Dual Serial: USB_DUAL_SERIAL OR (USB_SERIAL && USB_SERIAL2)
  #if defined(USB_DUAL_SERIAL) || (defined(USB_SERIAL) && defined(USB_SERIAL2))
    #define USB_OK 1
  #else
    #define USB_OK 0
  #endif
  #define USB_REQUIRED_TEXT "Dual Serial (Serial + Serial)"

#elif (REQUIRED_USB_MODE == 2)
// Serial + MIDI + Audio
  #if defined(USB_MIDI_AUDIO_SERIAL)
    #define USB_OK 1
  #else
    #define USB_OK 0
  #endif
  #define USB_REQUIRED_TEXT "Serial + MIDI + Audio"

#elif (REQUIRED_USB_MODE == 3)
// Triple Serial
  #if defined(USB_TRIPLE_SERIAL)
    #define USB_OK 1
  #else
    #define USB_OK 0
  #endif
  #define USB_REQUIRED_TEXT "Triple Serial"

#else
  #error "\n\n\n\n\n\nREQUIRED_USB_MODE must be 1 (Dual Serial), 2 (Serial + MIDI + Audio), or 3 (Triple Serial)\n\n\n\n\n\n"
#endif


// ---------- Error strings ----------
#if !BOARD_OK
  #define ERR_BOARD "\n\n\n - Wrong board: requires Teensy 4.1\n\n\n\n"
#else
  #define ERR_BOARD ""
#endif

#if !CPU_OK
  #define ERR_CPU "\n\n\n - Wrong CPU speed: set Tools > CPU Speed > 528 MHz\n\n\n\n"
#else
  #define ERR_CPU ""
#endif

#if !USB_OK
  #define ERR_USB "\n\n\n - USB Type must be: " USB_REQUIRED_TEXT "\n\n\n\n"
#else
  #define ERR_USB ""
#endif

// #if !OPT_OK
//   #define ERR_OPT "\n\n\n - Wrong optimization: set Tools > Optimize > Smallest Code with LTO\n\n\n\n"
// #else
//   #define ERR_OPT ""
#endif


// ---------- One combined failure ----------
#if !(BOARD_OK && CPU_OK && USB_OK )
static_assert(false,
  "\nConfiguration errors detected:\n\n"
  ERR_BOARD
  ERR_CPU
  ERR_USB
  // ERR_OPT
);
#endif