/*
 * Rotary encoder library for Arduino.
 */


#ifndef G0ORX_ROTARY_H
#define G0ORX_ROTARY_H

/*
 * Note: BOURN encoders have their A/B pins reversed compared to cheaper encoders.
 */

//#define BOURN_ENCODERS

// Enable weak pullups
#define ENABLE_PULLUPS


// Values returned by 'process'
// No complete step yet.
#define DIR_NONE 0
// Clockwise step.
#define DIR_CW 1
// Counter-clockwise step.
#define DIR_CCW -1


class G0ORX_Rotary
{
  public:
    G0ORX_Rotary();
    void updateA(unsigned char aState);
    void updateB(unsigned char bState);
    int process();

  private:
    int aLastState;
    int bLastState;
    int value=0;
};

#endif

