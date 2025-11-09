#ifndef FRONTPANEL_ROTARY_H
#define FRONTPANEL_ROTARY_H
#include "SDT.h"

/*
 * Note: BOURN encoders have their A/B pins reversed compared to cheaper encoders.
 */

#define BOURN_ENCODERS

// Enable weak pullups
#define ENABLE_PULLUPS

// Enable this to emit codes twice per step.
//#define HALF_STEP

// Values returned by 'process'
// No complete step yet.
#define DIR_NONE 0
// Clockwise step.
#define DIR_CW 1
// Counter-clockwise step.
#define DIR_CCW 2

/**
 * @brief Rotary encoder handler for quadrature encoders with interrupt-driven state tracking
 * @note Based on Ben Buxton's rotary encoder library, modified for MCP23017 GPIO expander
 * @note Supports BOURN encoders (A/B pins reversed) and directional reversal
 * @note All update methods are interrupt-safe and optimized with FASTRUN
 */
class Rotary_V12 {
public:
  /**
   * @brief Construct a rotary encoder handler
   * @param reversed Set to true to reverse rotation direction (CW becomes CCW and vice versa)
   * @note Initializes internal state and sets directional mode for encoder
   */
  Rotary_V12(bool reversed);

  /**
   * @brief Update encoder state when pin A changes
   * @param aState Combined encoder state: bits represent [A B] pins (0b00, 0b01, 0b10, 0b11)
   * @note Called from interrupt handler when encoder pin A state changes
   * @note Detects clockwise rotation leading edge and completes counter-clockwise rotation
   */
  void updateA(unsigned char aState);

  /**
   * @brief Update encoder state when pin B changes
   * @param bState Combined encoder state: bits represent [A B] pins (0b00, 0b01, 0b10, 0b11)
   * @note Called from interrupt handler when encoder pin B state changes
   * @note Detects counter-clockwise rotation leading edge and completes clockwise rotation
   */
  void updateB(unsigned char bState);

  /**
   * @brief Read and reset accumulated rotation value
   * @return Number of encoder steps: positive for clockwise, negative for counter-clockwise, 0 for no change
   * @note This function is interrupt-safe (disables interrupts during read)
   * @note Resets internal counter to zero after reading
   * @note Call periodically from main loop to retrieve rotation events
   */
  int process();

private:
  int aLastState;
  int bLastState;
  int value;
  bool _reversed;
};

#endif