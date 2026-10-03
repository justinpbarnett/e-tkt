#pragma once

// The second adapter for Background in src/Motion.h, beside Feeder: what
// runs while a motor turns, as the tape does while the wheel turns. It moves
// nothing, and only notes each turn it got: when it came, and where the motor
// it watches was then. A test can give it a job of so many turns, which is
// under way until it has had them.

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
  // How many more turns the job it stands for takes. None, until a test
  // gives it one.
  long turnsLeft = 0;

  void keepGoing() override {
    this->atUs.push_back(micros());
    if (this->motor != nullptr) {
      this->positions.push_back(this->motor->currentPosition());
    }
    if (this->turnsLeft > 0) {
      this->turnsLeft--;
    }
  }
  void finish() override { this->turnsLeft = 0; }
  bool underWay() const override { return this->turnsLeft > 0; }

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
