#pragma once

// The second adapter for each interface in src/Drivers.h.
//
// ArduinoServo and ArduinoStepper are the first; these are the second, which
// is what makes those two interfaces real seams rather than hypothetical
// ones. They record what the firmware asked the hardware to do, stamped with
// the virtual clock from test/stubs/Arduino.h, so a test can assert on the
// sequence and on how long each part of it took.

#include <AccelStepper.h>
#include <stdint.h>

#include <functional>
#include <vector>

#include "Arduino.h"
#include "Drivers.h"
#include "Motion.h"

// The estimates count each turn of a loop that keeps a motor going as
// MOTOR_LOOP_US, and on the host a turn takes as long as the stub's yield().
// The tests that hold an estimate to the time the fakes took need the two to
// agree.
static_assert(STUB_YIELD_US == MOTOR_LOOP_US,
              "The host's yield() must take as long as the estimates count "
              "for a turn of the motor loop");

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

  // Called after every write(), once it is in calls, which is where a test
  // notes where the daisy wheel stood as the press came down. See
  // StrokeLog.h.
  std::function<void()> afterWrite;

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
    if (this->afterWrite) {
      this->afterWrite();
    }
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
 * @brief Every stepper call, in order, and every step.
 *
 * run() is called far more often than it steps, so what is recorded of it
 * is the steps it takes rather than the calls: STEP, with the direction, +1
 * or -1. Everything else is recorded call for call.
 */
struct StepperCall {
  enum Kind {
    SET_MAX_SPEED,
    SET_ACCELERATION,
    SET_PINS_INVERTED,
    SET_ENABLE_PIN,
    SET_CURRENT_POSITION,
    MOVE,
    STEP,
    ENABLE_OUTPUTS,
    DISABLE_OUTPUTS
  };
  Kind kind;
  long value;
  unsigned long atMs;
};

/**
 * @brief A stepper that steps when AccelStepper would, because it keeps one
 * inside to say when.
 *
 * That is the library the board builds against, at the version
 * platformio.ini pins, on its functional interface, which leaves each step to
 * a function of the caller's. Here that function does nothing, so the
 * library only keeps the speed state and does its sums: run() steps once
 * micros() has moved on by the step interval, speeding up and slowing down on
 * the board's own curve. Time passes here only when something moves the
 * clock on, and every loop that drives a motor does, a yield() or a
 * delayMicroseconds() each time round. So a move takes as long in the tests
 * as it does on the machine, and two motors driven from one loop run side by
 * side.
 *
 * As on the board, the defaults before setMaxSpeed() and setAcceleration()
 * are a step a second, which is a crawl. Both firmware callers set their own
 * in initialize().
 */
class FakeStepper : public StepperDriver {
 private:
  // What the library calls to take each step, forward or back.
  static void stepNothing() {}

  AccelStepper library;

  void record(StepperCall::Kind kind, long value) {
    StepperCall c = {kind, value, millis()};
    this->calls.push_back(c);
  }

 public:
  std::vector<StepperCall> calls;
  bool energized = false;

  // Where the shaft physically is, in steps from where it started: every
  // step run() has taken, signed. setCurrentPosition() renames where the
  // motor is without turning it, so it leaves this alone. This is the
  // position the hall sensor and the press see. See FakeMagnet.h.
  long shaft = 0;

  // Called after every step run() takes, which is where a test raises a stop
  // partway through a move: on the board the stop arrives from another task
  // while the motor turns, and here nothing else is running.
  std::function<void()> afterStep;

  FakeStepper() : library(stepNothing, stepNothing) {}

  // What the firmware last set, AccelStepper's defaults until then.
  float maxSpeed() { return this->library.maxSpeed(); }
  float acceleration() { return this->library.acceleration(); }

  void setMaxSpeed(float speed) override {
    this->record(StepperCall::SET_MAX_SPEED, (long)speed);
    this->library.setMaxSpeed(speed);
  }

  void setAcceleration(float acceleration) override {
    this->record(StepperCall::SET_ACCELERATION, (long)acceleration);
    this->library.setAcceleration(acceleration);
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

  long currentPosition() override { return this->library.currentPosition(); }

  void setCurrentPosition(long position) override {
    this->library.setCurrentPosition(position);
    this->record(StepperCall::SET_CURRENT_POSITION, position);
  }

  void move(long relative) override {
    this->record(StepperCall::MOVE, relative);
    this->library.move(relative);
  }

  bool run() override {
    const long before = this->library.currentPosition();
    const bool turning = this->library.run();
    const long step = this->library.currentPosition() - before;
    if (step != 0) {
      this->shaft += step;
      this->record(StepperCall::STEP, step);
      if (this->afterStep) {
        this->afterStep();
      }
    }
    return turning;
  }

  long distanceToGo() override { return this->library.distanceToGo(); }

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

  /** @brief Index of the last call of that kind, or -1. */
  int lastIndexOf(StepperCall::Kind kind) const {
    for (size_t i = this->calls.size(); i > 0; i--) {
      if (this->calls[i - 1].kind == kind) {
        return (int)(i - 1);
      }
    }
    return -1;
  }

  void clear() { this->calls.clear(); }
};
