#pragma once

// Stand-in for the ESP32 core's Preferences, which keeps small values in the
// NVS partition of the flash. The partition here is stubNvs() in Arduino.h,
// and stubNvsText() for the text in it: it outlives any one Settings or Roll,
// the way flash outlives a reboot, and stubReset() wipes it. Every write is
// counted in stubNvsWrites().
//
// Like the real one, it does nothing outside begin() and end(). A module that
// forgets begin() reads its defaults and writes nothing, on the host as on
// the board.

#include <stddef.h>
#include <string.h>

#include <map>
#include <string>

#include "Arduino.h"

class Preferences {
 private:
  std::string space;
  bool started = false;
  bool readOnly = false;

 public:
  bool begin(const char* name, bool readOnly = false,
             const char* partitionLabel = NULL) {
    (void)partitionLabel;
    if (this->started) {
      return false;
    }
    this->space = name;
    this->started = true;
    this->readOnly = readOnly;
    return true;
  }

  void end() { this->started = false; }

  bool isKey(const char* key) {
    if (!this->started) {
      return false;
    }
    const std::map<std::string, uint32_t>& keys = stubNvs()[this->space];
    const std::map<std::string, std::string>& texts =
        stubNvsText()[this->space];
    return keys.find(key) != keys.end() || texts.find(key) != texts.end();
  }

  size_t putUInt(const char* key, uint32_t value) {
    if (!this->started || this->readOnly) {
      return 0;
    }
    stubNvs()[this->space][key] = value;
    stubNvsWrites()[this->space]++;
    return sizeof(value);
  }

  uint32_t getUInt(const char* key, uint32_t defaultValue = 0) {
    if (!this->started) {
      return defaultValue;
    }
    const std::map<std::string, uint32_t>& keys = stubNvs()[this->space];
    const std::map<std::string, uint32_t>::const_iterator found =
        keys.find(key);
    return found == keys.end() ? defaultValue : found->second;
  }

  size_t putBool(const char* key, bool value) {
    return this->putUInt(key, value ? 1 : 0) == 0 ? 0 : sizeof(uint8_t);
  }

  bool getBool(const char* key, bool defaultValue = false) {
    return this->getUInt(key, defaultValue ? 1 : 0) != 0;
  }

  size_t putString(const char* key, const char* value) {
    if (!this->started || this->readOnly) {
      return 0;
    }
    stubNvsText()[this->space][key] = value;
    stubNvsWrites()[this->space]++;
    return strlen(value);
  }

  String getString(const char* key, const String defaultValue = String()) {
    if (!this->started) {
      return defaultValue;
    }
    const std::map<std::string, std::string>& texts =
        stubNvsText()[this->space];
    const std::map<std::string, std::string>::const_iterator found =
        texts.find(key);
    return found == texts.end() ? defaultValue : String(found->second);
  }
};
