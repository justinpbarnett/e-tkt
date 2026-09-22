#pragma once

// Stand-in for the ESP32 analogWrite shim that Light.cpp includes. Writes are
// recorded on the virtual clock rather than reaching a pin; see Arduino.h.

#include "Arduino.h"

inline void analogWrite(uint8_t pin, uint32_t value) {
  StubPinWrite w = {pin, (int)value, stubClockMs()};
  stubAnalogWrites().push_back(w);
}
