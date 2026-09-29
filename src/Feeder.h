#pragma once

#include <Arduino.h>

#include "Configuration.h"
#include "Drivers.h"
#include "Logger.h"
#include "StopSignal.h"

/**
 * @brief Controls the feeder stepper motor.
 *
 * This motor feeds the label tape in only one direction
 * and never needs to maintain its position.
 */
class Feeder {
 private:
  Logger* logger;
  // Not owned; see LabelMaker.cpp.
  StepperDriver* stepper;
  StopSignal* stop;
  long fed = 0;

 public:
  Feeder(Logger* logger, StepperDriver* stepper, StopSignal* stop);
  ~Feeder();
  void initialize();

  /**
   * @brief Pushes the tape forward `repeat` feeds, and leaves the motor
   * free.
   *
   * A stop halts the motor within a step and skips the feeds still to come.
   * One already up when this is called feeds nothing at all.
   */
  void feed(int repeat = 1);
  void deenergize();

  /**
   * @brief Every feed since power-on that actually moved the tape.
   *
   * The machine has no way to see the tape, so this count is the only
   * evidence of how much of the roll has been used. It is counted here,
   * where the motor turns, rather than by each command that asks for a feed:
   * four commands feed, and a count kept by the callers would be right only
   * until the next one was added. With ENABLE_FEED off nothing moves, so
   * nothing is counted. A feed a stop cut short counts as a whole one if the
   * motor turned at all.
   */
  long feeds() const;
};
