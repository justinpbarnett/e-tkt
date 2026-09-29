#pragma once

#include "Arduino.h"
#include "Configuration.h"
#include "Drivers.h"
#include "HallSwitch.h"
#include "Logger.h"
#include "StopSignal.h"

/**
 * @brief Controls the daisy wheel stepper motor and computes the geometry
 * necessary for moving it into the correct position.
 */
class DaisyWheel {
 private:
  Logger* logger;
  HallSwitch* hall;
  // Not owned; see LabelMaker.cpp.
  StepperDriver* stepper;
  StopSignal* stop;
  const int stepsPerRevolution = CHAR_STEP_COUNT * CHAR_MICROSTEPS;
  float stepsPerChar = 0;
  // -1 is "no slot under the press", which is what deenergize() leaves
  // behind and what move() treats as "nowhere to count from, home first".
  // Every sibling here carries a starting value; this one did not, so
  // before home() ran it held whatever the allocation had in it.
  int currentChar = -1;
  bool homed = false;

  /**
   * @brief Forgets where the wheel is, after a stop left it somewhere
   * between two places. The next move() homes before it counts from here.
   */
  void lose();

 public:
  DaisyWheel(Logger* logger, HallSwitch* hall, StepperDriver* stepper,
             StopSignal* stop);
  ~DaisyWheel();

  /**
   * @brief Starts the hall sensor and sets up the stepper. The wheel does
   * not move: the first home() is the caller's, at the align it wants.
   */
  void initialize();

  /**
   * @brief Moves the daisy wheel home, which should be the letter "J".
   *
   * A stop halts the wheel within a step, wherever it has got to, and leaves
   * it unreferenced.
   */
  void home(int align);

  /**
   * @brief Moves the daisy wheel to the provided character "c".
   *
   * Returns false if the wheel did not get there: the wheel does not carry
   * the character, or a stop halted it on the way. Either way the press must
   * not come down.
   */
  bool move(String c, int alignFactor);

  /**
   * @brief True if the last home() found the hall trigger. False means the
   * wheel swept its full search range without seeing the magnet, or a stop
   * cut the homing short, and either way its position is unreferenced.
   */
  bool isHomed() { return this->homed; }

  /**
   * @brief Deactivates the daisy wheel stepper motor, potentially losing its
   * position.
   */
  void deenergize();
};
