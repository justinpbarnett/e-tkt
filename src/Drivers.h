#pragma once

#include <stdint.h>

// The seam between this firmware and the motor libraries it drives.
//
// Nothing here includes Arduino.h, AccelStepper.h or ESP32Servo.h, which is
// the whole point: Press, Feeder and DaisyWheel can be compiled and exercised
// on a development machine with no board attached. The real adapters live in
// ArduinoDrivers.h and are built only for the two firmware environments;
// recording fakes live in test/fakes and are built only for the host tests.
//
// Both interfaces are deliberately the exact set of calls the firmware makes
// and no more. Adding a method here because the underlying library has one is
// how a seam like this stops being useful.

/**
 * @brief The servo the press arm hangs off.
 *
 * Ordering: attach() before any write(), and again after every detach().
 * Press::initialize() does the first attach, Press::engage() does the rest.
 *
 * write() takes degrees, 0 to 180, and is a request rather than a command: a
 * hobby servo has no position feedback, so nothing here reports where the horn
 * actually ended up, and an angle the linkage cannot reach is simply held
 * against its stop. Reaching an angle takes real time, which is why Press
 * writes the same angle repeatedly rather than writing it once and moving on.
 *
 * detach() stops driving the horn so it can be back-driven by hand. A servo
 * that is still driving will strip its own gears if forced.
 */
class ServoDriver {
 public:
  virtual ~ServoDriver() {}
  virtual void attach(int pin) = 0;
  virtual void detach() = 0;
  virtual void write(int angle) = 0;
};

/**
 * @brief One stepper motor, addressed in steps.
 *
 * Ordering: setMaxSpeed() and setAcceleration() before any motion call, and
 * enableOutputs() before the motor will hold or move at all. Both callers do
 * this in their initialize().
 *
 * Two kinds of motion sit behind this interface and they behave very
 * differently. runToNewPosition() blocks until it arrives. move() only sets a
 * target, and the caller then drives run() round a loop until distanceToGo()
 * reaches zero -- which is what lets DaisyWheel watch the hall sensor while
 * the wheel is still turning.
 *
 * disableOutputs() drops the holding current. The motor is then free to be
 * turned by the tape or by hand, so any position it was holding is no longer
 * trustworthy; DaisyWheel treats a deenergize() as losing its home.
 */
class StepperDriver {
 public:
  virtual ~StepperDriver() {}

  // Configuration. Both are in steps per second and steps per second squared.
  virtual void setMaxSpeed(float speed) = 0;
  virtual void setAcceleration(float acceleration) = 0;
  virtual void setPinsInverted(bool directionInvert, bool stepInvert,
                               bool enableInvert) = 0;
  virtual void setEnablePin(uint8_t enablePin) = 0;

  // Position, in steps. setCurrentPosition() renames where the motor is now
  // without moving it, which is how homing establishes a zero.
  virtual long currentPosition() = 0;
  virtual void setCurrentPosition(long position) = 0;

  // Blocking motion: returns once the motor has arrived.
  virtual void runToNewPosition(long position) = 0;

  // Non-blocking motion: move() sets the target, run() advances at most one
  // step and returns true while the motor is still stepping, and
  // distanceToGo() counts down to zero.
  virtual void move(long relative) = 0;
  virtual bool run() = 0;
  virtual long distanceToGo() = 0;

  // Coil current.
  virtual void enableOutputs() = 0;
  virtual void disableOutputs() = 0;
};
