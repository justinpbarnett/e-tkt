#pragma once

// A development-machine stand-in for the Arduino core, just big enough to
// compile and run the job runner and every module it drives off the board.
//
// This is not an emulator and does not try to be. It supplies the handful of
// names those modules reach for, and AccelStepper with them, and it makes the
// ones that matter observable:
//
//   - delay() does not sleep. It advances a virtual clock, which millis()
//     reads back. A test can therefore assert how long the press holds at its
//     peak -- the stall-current question in Press.h that nothing could check
//     before -- and the whole suite still finishes in milliseconds. A test can
//     also hook it, to make something happen partway through a wait.
//   - yield() and delayMicroseconds() move the same clock on, by what a turn
//     of a loop costs on the board and by the wait. Every loop that drives a
//     motor goes through one or the other each time round, and the fake
//     steppers step on AccelStepper's schedule against this clock, so a move
//     takes as long here as it does on the machine. See FakeStepper.
//   - analogWrite() records every write, so the LED behaviour Press documents
//     can be asserted rather than assumed.
//   - analogRead() and digitalRead() answer whatever a test scripts, which is
//     how a test puts the magnet in front of the hall sensor, or takes it
//     away.
//   - The flash that Preferences keeps settings in is a map here, and it
//     outlives any one module the way flash outlives a reboot. ESP32Tone
//     records every note, and ESP.restart() counts the reboots it would have
//     done.
//   - The heap has as much memory free as a test says it has.
//
// Everything is header-only and every piece of recorded state lives in a
// function-local static, so there is no companion .cpp to keep in the build.
// Call stubReset() in setUp().

#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

// The ESP32 core's Arduino.h brings these in the same way, rather than as the
// macros the AVR core defines.
using std::abs;
using std::max;
using std::min;

// And these two as the core has them, for AccelStepper, which the fake
// steppers keep inside to say when a step is due. See FakeStepper.
typedef bool boolean;
#define constrain(amt, low, high) \
  ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

// --- pin modes and levels --------------------------------------------------

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05

// --- the virtual clock -----------------------------------------------------

inline unsigned long& stubClockMs() {
  static unsigned long ms = 0;
  return ms;
}

// Called after every delay(), which is where a test raises a stop partway
// through something that only waits, such as a blink: on the board the stop
// arrives from another task while the command loop sleeps, and here nothing
// else is running.
inline std::function<void()>& stubAfterDelay() {
  static std::function<void()> hook;
  return hook;
}

// Called whenever the clock reaches a new millisecond, whatever moved it
// there, and once for each: a delay(500) is 500 calls. A move is a loop of
// yield()s with no delay() in it, so this is the hook that sees time pass
// while a motor turns: the simulator keeps the machine in step with the
// wall clock from here, and answers a request that arrives partway through
// a wait at the time it arrives. See src/simulator.
inline std::function<void()>& stubAfterTick() {
  static std::function<void()> hook;
  return hook;
}

// What the clock has counted that does not yet add up to a whole
// millisecond. Homing waits 100 us a step, and dropping the remainder would
// make a full sweep of the wheel take no time at all.
inline unsigned long& stubClockUs() {
  static unsigned long us = 0;
  return us;
}

// Moves the clock on by `us`, and calls the tick hook at each new
// millisecond on the way.
inline void stubAdvanceUs(unsigned long us) {
  const unsigned long total = stubClockUs() + us;
  stubClockUs() = total % 1000;
  for (unsigned long ms = total / 1000; ms > 0; ms--) {
    stubClockMs()++;
    if (stubAfterTick()) {
      stubAfterTick()();
    }
  }
}

inline void delay(unsigned long ms) {
  stubAdvanceUs(ms * 1000UL);
  if (stubAfterDelay()) {
    stubAfterDelay()();
  }
}

// A busy wait on the board, not a place the scheduler runs anything else, so
// unlike delay() it does not call the delay hook.
inline void delayMicroseconds(unsigned int us) { stubAdvanceUs(us); }

// What one turn of a loop that drives a motor costs on the board: the stop
// check, the run() that decides whether a step is due, and the yield() that
// lets the webserver's task in. A few microseconds; this is the estimate.
static const unsigned long STUB_YIELD_US = 5;

// On the board this hands the core to any other task that is ready. Here
// nothing else is running, so all it does is let the time a loop takes pass.
inline void yield() { stubAdvanceUs(STUB_YIELD_US); }

inline unsigned long millis() { return stubClockMs(); }
inline unsigned long micros() { return stubClockMs() * 1000UL + stubClockUs(); }

// What random() draws from, between howsmall and howbig - 1. The ESP32
// core's draws on the hardware random number generator. Unset, this one
// answers the bottom of the range every time, so no test depends on luck.
// The simulator sets a real generator, so that stop ids start somewhere new
// at every boot, as they do on the machine. See StoppedCommand::id.
inline std::function<long(long, long)>& stubRandom() {
  static std::function<long(long, long)> source;
  return source;
}

inline long random(long howsmall, long howbig) {
  return stubRandom() ? stubRandom()(howsmall, howbig) : howsmall;
}

// --- recorded pin writes ---------------------------------------------------

struct StubPinWrite {
  int pin;
  int value;
  unsigned long atMs;
};

inline std::vector<StubPinWrite>& stubAnalogWrites() {
  static std::vector<StubPinWrite> writes;
  return writes;
}

inline std::vector<StubPinWrite>& stubDigitalWrites() {
  static std::vector<StubPinWrite> writes;
  return writes;
}

inline std::vector<std::string>& stubSerialLines() {
  static std::vector<std::string> lines;
  return lines;
}

// --- scripted inputs -------------------------------------------------------

// What analogRead() and digitalRead() answer, pin by pin. Unset, every analog
// pin reads 0 and every digital pin LOW -- which on this machine's hall
// sensor is the magnet sitting in front of it, so a wheel homes at once.
inline std::function<int(uint8_t)>& stubAnalogRead() {
  static std::function<int(uint8_t)> hook;
  return hook;
}

inline std::function<int(uint8_t)>& stubDigitalRead() {
  static std::function<int(uint8_t)> hook;
  return hook;
}

// --- flash, sound and reboots ----------------------------------------------

// The NVS partition Preferences keeps its keys in: namespace, then key. See
// Preferences.h beside this file.
inline std::map<std::string, std::map<std::string, uint32_t>>& stubNvs() {
  static std::map<std::string, std::map<std::string, uint32_t>> nvs;
  return nvs;
}

// The text it keeps, the same way. A key holds a number or text, never both.
inline std::map<std::string, std::map<std::string, std::string>>&
stubNvsText() {
  static std::map<std::string, std::map<std::string, std::string>> nvs;
  return nvs;
}

// Every note ESP32Tone was asked for. See ESP32Tone.h beside this file.
struct StubTone {
  int pin;
  unsigned int frequency;
  unsigned long durationMs;
  unsigned long atMs;
};

inline std::vector<StubTone>& stubTones() {
  static std::vector<StubTone> tones;
  return tones;
}

// How many times the firmware asked the chip to reboot. On the board
// ESP.restart() does not return; here it does, and counts.
inline int& stubRestarts() {
  static int restarts = 0;
  return restarts;
}

// --- memory ----------------------------------------------------------------

// What the heap says it has free. See esp_heap_caps.h beside this file.
struct StubHeap {
  size_t freeBytes;
  size_t largestFreeBlockBytes;
};

inline StubHeap& stubHeap() {
  static StubHeap heap = {0, 0};
  return heap;
}

inline void stubReset() {
  stubClockMs() = 0;
  stubClockUs() = 0;
  stubAfterDelay() = nullptr;
  stubAfterTick() = nullptr;
  stubRandom() = nullptr;
  stubAnalogWrites().clear();
  stubDigitalWrites().clear();
  stubSerialLines().clear();
  stubAnalogRead() = nullptr;
  stubDigitalRead() = nullptr;
  stubNvs().clear();
  stubNvsText().clear();
  stubTones().clear();
  stubRestarts() = 0;
  stubHeap().freeBytes = 0;
  stubHeap().largestFreeBlockBytes = 0;
}

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t pin, uint8_t value) {
  StubPinWrite w = {pin, value, stubClockMs()};
  stubDigitalWrites().push_back(w);
}
inline int digitalRead(uint8_t pin) {
  return stubDigitalRead() ? stubDigitalRead()(pin) : LOW;
}
inline int analogRead(uint8_t pin) {
  return stubAnalogRead() ? stubAnalogRead()(pin) : 0;
}

// --- String ----------------------------------------------------------------
// Arduino's String, narrowed to what the modules under test actually call.
// None makes one of a float. The core writes a float to two places, where
// std::to_string() writes six, so one added here has to as well, or the
// simulator's log reads differently from the device's.

class String {
 private:
  std::string value;

 public:
  String() {}
  String(const char* s) : value(s == 0 ? "" : s) {}
  String(const std::string& s) : value(s) {}
  String(char c) : value(1, c) {}
  String(int v) : value(std::to_string(v)) {}
  String(long v) : value(std::to_string(v)) {}
  String(unsigned int v) : value(std::to_string(v)) {}
  String(unsigned long v) : value(std::to_string(v)) {}

  const char* c_str() const { return this->value.c_str(); }
  const std::string& str() const { return this->value; }
  unsigned int length() const { return (unsigned int)this->value.length(); }

  String substring(unsigned int from) const {
    if (from >= this->value.length()) {
      return String();
    }
    return String(this->value.substr(from));
  }
  String substring(unsigned int from, unsigned int to) const {
    if (from >= this->value.length() || to <= from) {
      return String();
    }
    return String(this->value.substr(from, to - from));
  }

  void toUpperCase() {
    for (size_t i = 0; i < this->value.length(); i++) {
      this->value[i] = (char)toupper((unsigned char)this->value[i]);
    }
  }

  void toLowerCase() {
    for (size_t i = 0; i < this->value.length(); i++) {
      this->value[i] = (char)tolower((unsigned char)this->value[i]);
    }
  }

  char charAt(unsigned int i) const { return this->value[i]; }
  char operator[](unsigned int i) const { return this->value[i]; }

  String& operator+=(const String& other) {
    this->value += other.value;
    return *this;
  }

  bool operator==(const String& other) const {
    return this->value == other.value;
  }
  bool operator!=(const String& other) const {
    return this->value != other.value;
  }

  // std::map<String, ...> needs an ordering. Arduino's String compares with
  // strcmp, so byte order is what the device sees when it walks CHARACTERS.
  bool operator<(const String& other) const {
    return this->value < other.value;
  }

  int indexOf(const String& needle) const {
    const size_t at = this->value.find(needle.value);
    return at == std::string::npos ? -1 : (int)at;
  }

  bool startsWith(const String& prefix) const {
    return this->value.compare(0, prefix.value.length(), prefix.value) == 0;
  }

  bool endsWith(const String& suffix) const {
    return this->value.length() >= suffix.value.length() &&
           this->value.compare(this->value.length() - suffix.value.length(),
                               suffix.value.length(), suffix.value) == 0;
  }
};

inline String operator+(const String& a, const String& b) {
  String out(a);
  out += b;
  return out;
}

// --- Serial ----------------------------------------------------------------

class StubSerial {
 public:
  void begin(unsigned long) {}
  void println(const String& message) {
    stubSerialLines().push_back(message.str());
  }
  void println(const char* message) {
    stubSerialLines().push_back(std::string(message));
  }
  void print(const String& message) {
    stubSerialLines().push_back(message.str());
  }
  void flush() {}
};

inline StubSerial& stubSerialInstance() {
  static StubSerial serial;
  return serial;
}

#define Serial stubSerialInstance()

// --- ESP -------------------------------------------------------------------

class StubEsp {
 public:
  void restart() { stubRestarts()++; }
};

inline StubEsp& stubEspInstance() {
  static StubEsp esp;
  return esp;
}

#define ESP stubEspInstance()
