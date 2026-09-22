#include "ArduinoDrivers.h"

#include <AccelStepper.h>
#include <ESP32Servo.h>

void ArduinoServo::attach(int pin) { this->servo.attach(pin); }

void ArduinoServo::detach() { this->servo.detach(); }

void ArduinoServo::write(int angle) { this->servo.write(angle); }

ArduinoStepper::ArduinoStepper(uint8_t interface, uint8_t pin1, uint8_t pin2,
                               uint8_t pin3, uint8_t pin4)
    : stepper(interface, pin1, pin2, pin3, pin4) {}

void ArduinoStepper::setMaxSpeed(float speed) {
  this->stepper.setMaxSpeed(speed);
}

void ArduinoStepper::setAcceleration(float acceleration) {
  this->stepper.setAcceleration(acceleration);
}

void ArduinoStepper::setPinsInverted(bool directionInvert, bool stepInvert,
                                     bool enableInvert) {
  this->stepper.setPinsInverted(directionInvert, stepInvert, enableInvert);
}

void ArduinoStepper::setEnablePin(uint8_t enablePin) {
  this->stepper.setEnablePin(enablePin);
}

long ArduinoStepper::currentPosition() {
  return this->stepper.currentPosition();
}

void ArduinoStepper::setCurrentPosition(long position) {
  this->stepper.setCurrentPosition(position);
}

void ArduinoStepper::runToNewPosition(long position) {
  this->stepper.runToNewPosition(position);
}

void ArduinoStepper::move(long relative) { this->stepper.move(relative); }

bool ArduinoStepper::run() { return this->stepper.run(); }

long ArduinoStepper::distanceToGo() { return this->stepper.distanceToGo(); }

void ArduinoStepper::enableOutputs() { this->stepper.enableOutputs(); }

void ArduinoStepper::disableOutputs() { this->stepper.disableOutputs(); }
