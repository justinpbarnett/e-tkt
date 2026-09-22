#pragma once

// The second adapter for each interface in src/Drivers.h.
//
// ArduinoServo and ArduinoStepper are the first; these are the second, which
// is what makes those two interfaces real seams rather than hypothetical
// ones. They record what the firmware asked the hardware to do, stamped with
// the virtual clock from test/stubs/Arduino.h, so a test can assert on the
// sequence and on how long each part of it took.

#include <stdint.h>

#include <vector>

#include "Arduino.h"
#include "Drivers.h"

/**
 * @brief Every servo call, in order, with the time it happened.
 */
struct ServoCall {
  enum Kind { ATTACH, DETACH, WRITE };
  Kind kind;
  int value;  // pin for ATTACH, angle for WRITE, unused for DETACH
  unsigned long atMs;
};

class FakeServo : public ServoDriver {
 public:
  std::vector<ServoCall> calls;
  bool attached = false;

  void attach(int pin) override {
    this->attached = true;
    ServoCall c = {ServoCall::ATTACH, pin, millis()};
    this->calls.push_back(c);
  }

  void detach() override {
    this->attached = false;
    ServoCall c = {ServoCall::DETACH, 0, millis()};
    this->calls.push_back(c);
  }

  void write(int angle) override {
    ServoCall c = {ServoCall::WRITE, angle, millis()};
    this->calls.push_back(c);
  }

  // --- helpers the tests read the recording through ------------------------

  /** @brief Just the written angles, in order. */
  std::vector<int> angles() const {
    std::vector<int> out;
    for (size_t i = 0; i < this->calls.size(); i++) {
      if (this->calls[i].kind == ServoCall::WRITE) {
        out.push_back(this->calls[i].value);
      }
    }
    return out;
  }

  /**
   * @brief How long the servo was told to stay at `angle` without interruption
   * -- the span from the first write of a run of that angle to the write that
   * ended the run. This is the number that decides whether the servo is
   * sitting stalled against the daisy wheel.
   */
  unsigned long longestHoldAt(int angle) const {
    unsigned long best = 0;
    bool inRun = false;
    unsigned long runStart = 0;
    for (size_t i = 0; i < this->calls.size(); i++) {
      const bool matches = this->calls[i].kind == ServoCall::WRITE &&
                           this->calls[i].value == angle;
      if (matches) {
        if (!inRun) {
          inRun = true;
          runStart = this->calls[i].atMs;
        }
        continue;
      }
      // Anything else -- a different angle, a detach -- ends the hold. The
      // run lasted until this call, not until the last write inside it, which
      // is why the ending call supplies the timestamp.
      if (inRun) {
        const unsigned long held = this->calls[i].atMs - runStart;
        if (held > best) {
          best = held;
        }
        inRun = false;
      }
    }
    if (inRun) {
      const unsigned long held = millis() - runStart;
      if (held > best) {
        best = held;
      }
    }
    return best;
  }

  /** @brief The deepest angle written, i.e. the smallest. */
  int minAngle() const {
    const std::vector<int> written = this->angles();
    int best = written.empty() ? 0 : written[0];
    for (size_t i = 0; i < written.size(); i++) {
      if (written[i] < best) {
        best = written[i];
      }
    }
    return best;
  }

  int maxAngle() const {
    const std::vector<int> written = this->angles();
    int best = written.empty() ? 0 : written[0];
    for (size_t i = 0; i < written.size(); i++) {
      if (written[i] > best) {
        best = written[i];
      }
    }
    return best;
  }

  void clear() {
    this->calls.clear();
    this->attached = false;
  }
};

/**
 * @brief Every stepper call, in order.
 *
 * Position is tracked so currentPosition() and the non-blocking move/run/
 * distanceToGo loop behave the way a caller expects; nothing else is
 * simulated.
 */
struct StepperCall {
  enum Kind {
    SET_MAX_SPEED,
    SET_ACCELERATION,
    SET_PINS_INVERTED,
    SET_ENABLE_PIN,
    SET_CURRENT_POSITION,
    RUN_TO_NEW_POSITION,
    MOVE,
    RUN,
    ENABLE_OUTPUTS,
    DISABLE_OUTPUTS
  };
  Kind kind;
  long value;
  unsigned long atMs;
};

class FakeStepper : public StepperDriver {
 private:
  long position = 0;
  long target = 0;

  void record(StepperCall::Kind kind, long value) {
    StepperCall c = {kind, value, millis()};
    this->calls.push_back(c);
  }

 public:
  std::vector<StepperCall> calls;
  bool energized = false;
  float maxSpeed = 0;
  float acceleration = 0;

  void setMaxSpeed(float speed) override {
    this->maxSpeed = speed;
    this->record(StepperCall::SET_MAX_SPEED, (long)speed);
  }

  void setAcceleration(float acceleration) override {
    this->acceleration = acceleration;
    this->record(StepperCall::SET_ACCELERATION, (long)acceleration);
  }

  void setPinsInverted(bool directionInvert, bool stepInvert,
                       bool enableInvert) override {
    // Packed so one recorded value carries all three flags.
    const long packed = (directionInvert ? 4 : 0) | (stepInvert ? 2 : 0) |
                        (enableInvert ? 1 : 0);
    this->record(StepperCall::SET_PINS_INVERTED, packed);
  }

  void setEnablePin(uint8_t enablePin) override {
    this->record(StepperCall::SET_ENABLE_PIN, enablePin);
  }

  long currentPosition() override { return this->position; }

  void setCurrentPosition(long position) override {
    this->position = position;
    this->target = position;
    this->record(StepperCall::SET_CURRENT_POSITION, position);
  }

  void runToNewPosition(long position) override {
    this->record(StepperCall::RUN_TO_NEW_POSITION, position);
    this->position = position;
    this->target = position;
  }

  void move(long relative) override {
    this->record(StepperCall::MOVE, relative);
    this->target = this->position + relative;
  }

  bool run() override {
    this->record(StepperCall::RUN, this->target - this->position);
    if (this->position == this->target) {
      return false;
    }
    this->position += (this->target > this->position) ? 1 : -1;
    return true;
  }

  long distanceToGo() override { return this->target - this->position; }

  void enableOutputs() override {
    this->energized = true;
    this->record(StepperCall::ENABLE_OUTPUTS, 0);
  }

  void disableOutputs() override {
    this->energized = false;
    this->record(StepperCall::DISABLE_OUTPUTS, 0);
  }

  // --- helpers the tests read the recording through ------------------------

  /** @brief Every value passed to calls of one kind, in order. */
  std::vector<long> valuesOf(StepperCall::Kind kind) const {
    std::vector<long> out;
    for (size_t i = 0; i < this->calls.size(); i++) {
      if (this->calls[i].kind == kind) {
        out.push_back(this->calls[i].value);
      }
    }
    return out;
  }

  int countOf(StepperCall::Kind kind) const {
    int n = 0;
    for (size_t i = 0; i < this->calls.size(); i++) {
      if (this->calls[i].kind == kind) {
        n++;
      }
    }
    return n;
  }

  /** @brief Index of the first call of that kind, or -1. */
  int firstIndexOf(StepperCall::Kind kind) const {
    for (size_t i = 0; i < this->calls.size(); i++) {
      if (this->calls[i].kind == kind) {
        return (int)i;
      }
    }
    return -1;
  }

  void clear() { this->calls.clear(); }
};
