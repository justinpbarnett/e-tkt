#pragma once

// The second adapter for Background in src/Motion.h, beside Feeder: what
// runs while a motor turns, as the tape does while the wheel turns. It moves
// nothing, and only notes each turn it got: when it came, and where the motor
// it watches was then.

#include <algorithm>
#include <vector>

#include "Arduino.h"
#include "Drivers.h"
#include "Motion.h"

class FakeBackground : public Background {
 public:
  // The motor whose position each turn notes, or none.
  explicit FakeBackground(StepperDriver* motor = nullptr) : motor(motor) {}

  // When each turn came, by micros().
  std::vector<unsigned long> atUs;
  // Where the motor was at each turn, if there is one to watch.
  std::vector<long> positions;

  void keepGoing() override {
    this->atUs.push_back(micros());
    if (this->motor != nullptr) {
      this->positions.push_back(this->motor->currentPosition());
    }
  }
  void finish() override {}

  // Forgets the turns so far.
  void clear() {
    this->atUs.clear();
    this->positions.clear();
  }

  // The longest it went without a turn from `fromUs` to `toUs`, by micros():
  // to the first turn, between two, and from the last to the end.
  unsigned long longestWaitUs(unsigned long fromUs, unsigned long toUs) const {
    unsigned long longest = 0;
    unsigned long last = fromUs;
    for (const unsigned long at : this->atUs) {
      longest = std::max(longest, at - last);
      last = at;
    }
    return std::max(longest, toUs - last);
  }

 private:
  StepperDriver* motor;
};
