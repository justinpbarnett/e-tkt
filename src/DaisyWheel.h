#pragma once

#include "Arduino.h"
#include "Configuration.h"
#include "Drivers.h"
#include "HallSwitch.h"
#include "Logger.h"
#include "StopSignal.h"

/**
 * @brief How a turn of the daisy wheel ended.
 */
enum class Turn {
  // The slot asked for is under the press.
  REACHED,
  // The wheel does not carry the slot asked for, so it did not turn.
  NO_SLOT,
  // A stop halted the wheel, wherever it had got to.
  STOPPED,
  // Homing swept for the magnet and did not find it, so nothing counted
  // from where the search gave up can be trusted.
  LOST,
};

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

  /**
   * @brief Forgets where the wheel is, after a stop or a search that found
   * no magnet left it somewhere unknown. The next move() homes before it
   * counts from here.
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
   * @brief Finds the magnet and turns the home character, the J, under the
   * press, offset by the align.
   *
   * REACHED once the J is there. LOST if a turn and a half of searching
   * never showed the magnet to the hall sensor. STOPPED if a stop halted
   * the wheel first, within a step, wherever it had got to. Either of the
   * last two leaves the wheel unreferenced.
   */
  Turn home(int align);

  /**
   * @brief Turns the slot for character "c" under the press, homing first
   * unless it is there already.
   *
   * Anything but REACHED means the wheel did not get there, and the press
   * must not come down: NO_SLOT if the wheel does not carry the character,
   * and otherwise however the homing or the turn after it ended.
   */
  Turn move(String c, int alignFactor);

  /**
   * @brief Deactivates the daisy wheel stepper motor, potentially losing its
   * position.
   */
  void deenergize();
};
