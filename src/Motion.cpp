#include "Motion.h"

#include <Arduino.h>
#include <limits.h>
#include <math.h>

namespace {

// One move's speed state, and AccelStepper's computeNewSpeed() line for line,
// in the library's own types, so that every interval rounds the way the
// library's does. The fake the host tests run the firmware on,
// test/fakes/FakeDrivers.h, has the same lines: that one is the motor, and
// this one counts for the estimates.
class Ramp {
 public:
  // What move() does, from rest.
  Ramp(const StepperTiming& timing, long distance)
      : acceleration(timing.acceleration),
        c0(0.676 * sqrt(2.0 / timing.acceleration) * 1000000.0),
        cmin(1000000.0 / timing.maxSpeed),
        distanceTo(distance) {
    this->computeNewSpeed();
  }

  // The interval to the next step in whole microseconds, or 0 once the motor
  // has arrived and stopped.
  unsigned long interval() const { return this->stepInterval; }

  // What run() does once the interval is up.
  void step() {
    this->distanceTo -= this->clockwise ? 1 : -1;
    this->computeNewSpeed();
  }

 private:
  const float acceleration;
  const float c0;
  const float cmin;
  long distanceTo;
  long n = 0;
  float cn = 0;
  float speed = 0;
  unsigned long stepInterval = 0;
  bool clockwise = false;

  void computeNewSpeed() {
    const long stepsToStop =
        (long)((this->speed * this->speed) / (2.0 * this->acceleration));
    if (this->distanceTo == 0 && stepsToStop <= 1) {
      this->stepInterval = 0;
      this->speed = 0;
      this->n = 0;
      return;
    }
    if (this->distanceTo > 0) {
      if (this->n > 0) {
        if (stepsToStop >= this->distanceTo || !this->clockwise) {
          this->n = -stepsToStop;
        }
      } else if (this->n < 0) {
        if (stepsToStop < this->distanceTo && this->clockwise) {
          this->n = -this->n;
        }
      }
    } else if (this->distanceTo < 0) {
      if (this->n > 0) {
        if (stepsToStop >= -this->distanceTo || this->clockwise) {
          this->n = -stepsToStop;
        }
      } else if (this->n < 0) {
        if (stepsToStop < -this->distanceTo && !this->clockwise) {
          this->n = -this->n;
        }
      }
    }
    if (this->n == 0) {
      this->cn = this->c0;
      this->clockwise = this->distanceTo > 0;
    } else {
      this->cn = this->cn - ((2.0 * this->cn) / ((4.0 * this->n) + 1));
      this->cn = max(this->cn, this->cmin);
    }
    this->n++;
    this->stepInterval = this->cn;
    this->speed = 1000000.0 / this->cn;
    if (!this->clockwise) {
      this->speed = -this->speed;
    }
  }
};

}  // namespace

unsigned long StepperTiming::firstStepUs() const {
  return Ramp(*this, 1).interval();
}

unsigned long StepperTiming::moveUs(long distance, unsigned long loopUs) const {
  return this->stepsUs(distance, LONG_MAX, loopUs);
}

unsigned long StepperTiming::stepsUs(long distance, long steps,
                                     unsigned long loopUs) const {
  if (distance == 0 || steps <= 0) {
    return 0;
  }
  Ramp ramp(*this, distance);
  ramp.step();
  unsigned long us = 0;
  for (long taken = 1; taken < steps && ramp.interval() != 0; taken++) {
    us += waitedUs(ramp.interval(), loopUs);
    ramp.step();
  }
  return us;
}
