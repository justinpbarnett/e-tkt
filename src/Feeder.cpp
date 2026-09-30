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

// The motor as initialize() sets it up, and as feedUs() counts it.
static const StepperTiming TIMING = {FEED_STEPPER_MAX_SPEED,
                                     FEED_STEPPER_MAX_ACCELERATION};

void Feeder::initialize() {
  this->stepper->setMaxSpeed(TIMING.maxSpeed);
  this->stepper->setAcceleration(TIMING.acceleration);
}

void Feeder::start(int feeds) {
  if (feeds <= 0) {
    return;
  }
  if (!ENABLE_FEED) {
    delay(500);
    return;
  }
  // Before anything is asked of the motor, so a stop that is already up
  // leaves an idle one exactly as it was. Feeds already under way are
  // keepGoing()'s to stop.
  if (this->stop->shouldStop()) {
    return;
  }

  this->logger->log(String("Feeding ") + feeds + "x...");

  this->pending += feeds;
  if (this->phase == Phase::IDLE) {
    this->stepper->enableOutputs();
    this->enter(Phase::SETTLING_ON);
  }
}

void Feeder::keepGoing() {
  if (this->phase == Phase::IDLE) {
    return;
  }
  const bool owed = this->phase == Phase::MOVING || this->pending > 0;
  if (owed && this->stop->shouldStop()) {
    this->haltFeed();
    this->pending = 0;
    if (this->phase != Phase::SETTLING_OFF) {
      // Let go of the tape, so whoever is clearing the jam can pull it.
      this->stepper->disableOutputs();
      this->enter(Phase::SETTLING_OFF);
    }
    return;
  }

  switch (this->phase) {
    case Phase::SETTLING_ON:
      if (this->waited(FEED_SETTLE_ON_MS)) {
        this->next();
      }
      break;
    case Phase::MOVING:
      if (!this->stepper->run()) {
        this->fed++;
        this->enter(Phase::GAP);
      }
      break;
    case Phase::GAP:
      if (this->waited(FEED_GAP_MS)) {
        this->next();
      }
      break;
    case Phase::SETTLING_OFF:
      if (this->waited(FEED_SETTLE_OFF_MS)) {
        if (this->pending > 0) {
          // More was asked for while the coils were letting go.
          this->stepper->enableOutputs();
          this->enter(Phase::SETTLING_ON);
        } else {
          this->phase = Phase::IDLE;
        }
      }
      break;
    case Phase::IDLE:
      break;
  }
}

void Feeder::finish() {
  while (this->phase != Phase::IDLE) {
    this->keepGoing();
    yield();
  }
}

void Feeder::feed(int repeat) {
  this->start(repeat);
  this->finish();
}

unsigned long Feeder::feedUs(int feeds) const {
  if (feeds <= 0) {
    return 0;
  }
  const unsigned long loop = MOTOR_LOOP_US;
  const unsigned long gap = waitedUs(FEED_GAP_MS * 1000UL, loop);
  // The coils come up, and the first step is taken the turn after.
  unsigned long us = waitedUs(FEED_SETTLE_ON_MS * 1000UL, loop) + loop;
  us += feeds * TIMING.moveUs(STEPS_PER_FEED, loop);
  // Between one feed and the next the gap, and the turn after it that asks
  // for the next move. Unless the motor is still too close to its last step
  // to take the first of a new move: the gap is shorter than that.
  const unsigned long between =
      max(waitedUs(TIMING.firstStepUs(), loop), gap + loop);
  us += (feeds - 1) * between;
  // After the last one the gap, the tape settling once the coils let go, and
  // the turn finish() takes to see that it has.
  us += gap + waitedUs(FEED_SETTLE_OFF_MS * 1000UL, loop) + loop;
  return us;
}

void Feeder::next() {
  if (this->pending == 0) {
    this->stepper->disableOutputs();
    this->enter(Phase::SETTLING_OFF);
    return;
  }
  this->pending--;
  this->moveStart = this->stepper->currentPosition();
  const long direction = REVERSE_FEED_STEPPER_DIRECTION ? 1 : -1;
  this->stepper->move(STEPS_PER_FEED * direction);
  this->phase = Phase::MOVING;
}

void Feeder::haltFeed() {
  if (this->phase != Phase::MOVING) {
    return;
  }
  halt(this->stepper);
  // A feed a stop cut short still pulled some tape through, so it counts as
  // a whole one. The roll is estimated from this count, and an estimate that
  // runs out a little early is better than one that runs out late.
  if (this->stepper->currentPosition() != this->moveStart) {
    this->fed++;
  }
}

void Feeder::enter(Phase phase) {
  this->phase = phase;
  this->phaseStartUs = micros();
}

bool Feeder::waited(unsigned long ms) const {
  return micros() - this->phaseStartUs >= ms * 1000UL;
}

void Feeder::deenergize() {
  this->haltFeed();
  this->pending = 0;
  this->stepper->disableOutputs();
  this->phase = Phase::IDLE;
}

long Feeder::feeds() const { return this->fed; }
