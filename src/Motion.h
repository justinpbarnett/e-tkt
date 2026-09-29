#pragma once

#include "Arduino.h"
#include "Drivers.h"
#include "StopSignal.h"

// Moving a stepper in a way a stop can interrupt.
//
// AccelStepper's own runToNewPosition() is a loop of run() calls that returns
// only once the motor arrives, and while it runs nothing else on the command
// loop gets a look in. This is the same loop with a stop check in it.

/**
 * @brief Halts a stepper where it is, at once.
 *
 * Renaming where the motor is as where it is going zeroes its speed, which
 * is AccelStepper's abrupt stop. AccelStepper::stop() is the gentle one: it
 * sets a target far enough ahead to slow down in, so the motor goes on
 * turning for as long as it took to speed up.
 */
inline void halt(StepperDriver* stepper) {
  stepper->setCurrentPosition(stepper->currentPosition());
}

/**
 * @brief Runs a stepper to an absolute position, unless a stop comes first.
 *
 * The motor accelerates and decelerates exactly as it does under
 * AccelStepper::runToNewPosition(), and the stop is checked before every
 * step, so it is obeyed within one step of being raised. Returns true if the
 * motor arrived, false if the stop halted it on the way, in which case it is
 * left wherever it had got to and still energised.
 */
inline bool runToNewPosition(StepperDriver* stepper, long position,
                             StopSignal* stop) {
  stepper->move(position - stepper->currentPosition());
  for (;;) {
    if (stop->shouldStop()) {
      halt(stepper);
      return false;
    }
    if (!stepper->run()) {
      return true;
    }
    yield();
  }
}
