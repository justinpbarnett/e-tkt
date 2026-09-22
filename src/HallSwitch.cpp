#include "HallSwitch.h"

#include <Arduino.h>

#include "Configuration.h"
#include "Logger.h"


HallSwitch::~HallSwitch() {}

HallSwitch::HallSwitch(Logger* logger, uint8_t pin) {
  this->logger = logger;
  this->pin = pin;
}

void HallSwitch::initialize() {
  pinMode(this->pin, INPUT_PULLUP);
  // GPIO34 has no internal pull-up, so this call does nothing -- the 10 k
  // pull-up from the module's OUT to the DevKit's 3V3 pin is what makes the
  // input swing. Measured 2026-09-18: magnet present 0, magnet clear 4095.
}

bool HallSwitch::triggered() {
  return (analogRead(this->pin) < HALL_SENSOR_THRESHOLD) ^
         INVERT_HALL_SENSOR_LOGIC;
}
