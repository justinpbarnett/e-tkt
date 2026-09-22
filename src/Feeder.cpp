#include "Feeder.h"

#include <Arduino.h>

#include "Configuration.h"
#include "Drivers.h"
#include "Logger.h"

Feeder::Feeder(Logger* logger, StepperDriver* stepper) {
  this->logger = logger;
  this->stepper = stepper;
}

Feeder::~Feeder() {
  // The stepper is handed in, not built here, so it is not ours to delete.
}

void Feeder::initialize() {
  this->stepper->setMaxSpeed(FEED_STEPPER_MAX_SPEED);
  this->stepper->setAcceleration(FEED_STEPPER_MAX_ACCELERATION);
}

void Feeder::feed(int repeat) {
  // runs the feed stepper by a specific amount to push the tape forward
  if (!ENABLE_FEED) {
    delay(500);
    return;
  }

  this->logger->log(String("Feeding ") + repeat + "x...");

  this->stepper->enableOutputs();
  delay(10);

  int direction = -1;
  if (REVERSE_FEED_STEPPER_DIRECTION) {
    direction = 1;
  }
  for (int i = 0; i < repeat; i++) {
    this->stepper->runToNewPosition(this->stepper->currentPosition() +
                                    (FEED_MOTOR_STEPS_PER_REVOLUTION / 8) *
                                        direction);
    delay(10);
  }

  this->deenergize();

  delay(20);
}

void Feeder::deenergize() { this->stepper->disableOutputs(); }
