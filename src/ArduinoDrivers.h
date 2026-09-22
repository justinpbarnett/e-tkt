#pragma once

#include <AccelStepper.h>
#include <ESP32Servo.h>
#include <stdint.h>

#include "Drivers.h"

// The on-board adapters: thin forwarding onto ESP32Servo and AccelStepper.
//
// These are the only files in src/ that name those two libraries, so they are
// also the only files that cannot be compiled on a development machine. Keep
// them free of decisions -- anything worth testing belongs on the far side of
// the interface, in Press, Feeder or DaisyWheel.

/**
 * @brief ServoDriver backed by ESP32Servo.
 */
class ArduinoServo : public ServoDriver {
 private:
  Servo servo;

 public:
  void attach(int pin) override;
  void detach() override;
  void write(int angle) override;
};

/**
 * @brief StepperDriver backed by AccelStepper.
 *
 * The constructor arguments are AccelStepper's own, defaults included, so a
 * call site reads the same as the `new AccelStepper(...)` it replaced. The
 * daisy wheel is a step/direction driver and passes two pins; the feeder
 * drives its coils directly and passes four.
 */
class ArduinoStepper : public StepperDriver {
 private:
  AccelStepper stepper;

 public:
  ArduinoStepper(uint8_t interface, uint8_t pin1, uint8_t pin2,
                 uint8_t pin3 = 4, uint8_t pin4 = 5);

  void setMaxSpeed(float speed) override;
  void setAcceleration(float acceleration) override;
  void setPinsInverted(bool directionInvert, bool stepInvert,
                       bool enableInvert) override;
  void setEnablePin(uint8_t enablePin) override;

  long currentPosition() override;
  void setCurrentPosition(long position) override;

  void runToNewPosition(long position) override;

  void move(long relative) override;
  bool run() override;
  long distanceToGo() override;

  void enableOutputs() override;
  void disableOutputs() override;
};
