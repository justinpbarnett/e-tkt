#include "ArduinoDrivers.h"

#include <AccelStepper.h>
#include <ESP32Servo.h>
#include <soc/ledc_struct.h>

void ArduinoServo::attach(int pin) {
  this->pin = pin;
  this->servo.attach(pin);
}

// The LEDC channel driving `pin`, or -1 if none is.
static int channelOn(int pin) {
  for (int chan = 0; chan < NUM_PWM; chan++) {
    ESP32PWM* pwm = ESP32PWM::ChannelUsed[chan];
    if (pwm != NULL && pwm->attached() && pwm->getPin() == pin) {
      return chan;
    }
  }
  return -1;
}

// Waits for the pulse going out on `chan`, if one is, to end. Cut off part
// way, a pulse is a shorter one, and a shorter pulse is an angle further in,
// toward the tape, which the servo would go to as it was let go. A pulse is at
// most 2.5 ms of every 20, so this waits no longer than that, and the next one
// is more than 17 ms away when it returns. The channel and its timer are
// mapped as esp32-hal-ledc.c maps them, and the output is high from 0 to the
// duty.
static void waitForThePulseToEnd(int chan) {
  const int group = chan / 8;
  const int timer = (chan / 2) % 4;
  const uint32_t duty =
      LEDC.channel_group[group].channel[chan % 8].duty_rd.duty_read >> 4;
  const unsigned long start = micros();
  while (LEDC.timer_group[group].timer[timer].value.timer_cnt < duty &&
         micros() - start < 3000) {
  }
}

void ArduinoServo::detach() {
  const int chan = channelOn(this->pin);
  if (chan >= 0) {
    waitForThePulseToEnd(chan);
  }
  this->servo.detach();
}

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

void ArduinoStepper::move(long relative) { this->stepper.move(relative); }

bool ArduinoStepper::run() { return this->stepper.run(); }

long ArduinoStepper::distanceToGo() { return this->stepper.distanceToGo(); }

void ArduinoStepper::enableOutputs() { this->stepper.enableOutputs(); }

void ArduinoStepper::disableOutputs() { this->stepper.disableOutputs(); }
