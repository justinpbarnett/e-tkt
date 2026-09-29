#include "Feeder.h"

#include <Arduino.h>

#include "Configuration.h"
#include "Drivers.h"
#include "Logger.h"
#include "Motion.h"
#include "StopSignal.h"

Feeder::Feeder(Logger* logger, StepperDriver* stepper, StopSignal* stop) {
  this->logger = logger;
  this->stepper = stepper;
  this->stop = stop;
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
  // Before the coils are powered, so a stop that is already up leaves the
  // motor exactly as it was.
  if (this->stop->shouldStop()) {
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
    const long start = this->stepper->currentPosition();
    const bool arrived = runToNewPosition(
        this->stepper,
        start + (FEED_MOTOR_STEPS_PER_REVOLUTION / 8) * direction, this->stop);
    // A feed a stop cut short still pulled some tape through, so it counts
    // as a whole one. The roll is estimated from this count, and an estimate
    // that runs out a little early is better than one that runs out late.
    if (arrived || this->stepper->currentPosition() != start) {
      this->fed++;
    }
    if (!arrived) {
      break;
    }
    delay(10);
  }

  this->deenergize();

  delay(20);
}

void Feeder::deenergize() { this->stepper->disableOutputs(); }

long Feeder::feeds() const { return this->fed; }
