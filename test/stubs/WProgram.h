#pragma once

// The name the Arduino core had before 1.0, which AccelStepper.h includes, and
// wiring.h with it, wherever ARDUINO is not defined. It is not on the host, so
// here the two stand for Arduino.h.
//
// Defining ARDUINO for the host would send the library to Arduino.h instead,
// but ArduinoJson reads it too, and would reach for the core's Stream, Print
// and flash strings, which the stubs do not have.

#include "Arduino.h"
