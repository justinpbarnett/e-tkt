#pragma once

// A development-machine stand-in for the Arduino core, just big enough to
// compile and run Press, Feeder, Light and Logger off the board.
//
// This is not an emulator and does not try to be. It supplies the handful of
// names those four modules reach for, and it makes two of them observable:
//
//   - delay() does not sleep. It advances a virtual clock, which millis()
//     reads back. A test can therefore assert how long the press holds at its
//     peak -- the stall-current question in Press.h that nothing could check
//     before -- and the whole suite still finishes in milliseconds.
//   - analogWrite() records every write, so the LED behaviour Press documents
//     can be asserted rather than assumed.
//
// Everything is header-only and every piece of recorded state lives in a
// function-local static, so there is no companion .cpp to keep in the build.
// Call stubReset() in setUp().

#include <stdint.h>
#include <stdlib.h>

#include <string>
#include <vector>

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

inline void delay(unsigned long ms) { stubClockMs() += ms; }
inline unsigned long millis() { return stubClockMs(); }
inline unsigned long micros() { return stubClockMs() * 1000UL; }

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

inline void stubReset() {
  stubClockMs() = 0;
  stubAnalogWrites().clear();
  stubDigitalWrites().clear();
  stubSerialLines().clear();
}

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t pin, uint8_t value) {
  StubPinWrite w = {pin, value, stubClockMs()};
  stubDigitalWrites().push_back(w);
}
inline int digitalRead(uint8_t) { return LOW; }
inline int analogRead(uint8_t) { return 0; }

// --- String ----------------------------------------------------------------
// Arduino's String, narrowed to what the modules under test actually call.

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
  String(float v) : value(std::to_string(v)) {}
  String(double v) : value(std::to_string(v)) {}

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
