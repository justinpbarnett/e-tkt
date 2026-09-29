#pragma once

// Stand-in for ESP32Servo's tone(), which the sounder plays through. Every
// note is recorded on the virtual clock, so a test can ask what the machine
// played and when.
//
// The real three-argument tone() starts the note, delay()s for its length
// and stops it, so a note holds up whatever called it. This one does the
// same delay(), which moves the clock on and gives a test's hook its turn
// while the note sounds. See Arduino.h.

#include "Arduino.h"

inline void tone(int pin, unsigned int frequency) {
  StubTone t = {pin, frequency, 0, stubClockMs()};
  stubTones().push_back(t);
}

inline void noTone(int) {}

inline void tone(int pin, unsigned int frequency, unsigned long duration) {
  StubTone t = {pin, frequency, duration, stubClockMs()};
  stubTones().push_back(t);
  delay(duration);
  noTone(pin);
}
