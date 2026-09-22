#pragma once

#include <Arduino.h>

#include "Configuration.h"
#include "Drivers.h"
#include "Logger.h"

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

 public:
  Feeder(Logger* logger, StepperDriver* stepper);
  ~Feeder();
  void initialize();
  void feed(int repeat = 1);
  void deenergize();
};
