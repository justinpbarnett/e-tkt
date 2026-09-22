#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <tuple>

#include "Configuration.h"
#include "Logger.h"
#include "PressGeometry.h"

// Both are CALIBRATION_VALUE_MIN..CALIBRATION_VALUE_MAX (1-9). Used only the
// first time the device boots, before the calibration keys exist in EEPROM.
#define DEFAULT_ALIGN_FACTOR 5  // 1 to 9, 5 is the mid value
#define DEFAULT_FORCE_FACTOR 1  // 1 to 9

/**
 * @brief Stores and retrieves settings from EEPROM using the
 * preference library.
 */
class Settings {
 private:
  Logger* logger;
  Preferences* preferences = new Preferences();
  uint32_t alignFactor = DEFAULT_ALIGN_FACTOR;
  uint32_t forceFactor = DEFAULT_FORCE_FACTOR;

 public:
  Settings(Logger* logger);
  ~Settings();

  /**
   * @brief Initializes the settings object by loading the settings from EEPROM.
   */
  void initialize();

  /**
   * @brief Returns the current alginment calbration value
   */
  uint32_t getAlignFactor();

  /**
   * @brief Returns the current force calibration value
   */
  uint32_t getForceFactor();

  /**
   * @brief Saves the given alignment and force calibration values to EEPROM.
   *
   * Both are clamped into 1-9 first and the clamp is logged. The HTTP
   * handlers already refuse out-of-range values, so anything that arrives
   * here out of range is a bug; clamping keeps the machine usable rather
   * than storing a force that computes a peak short of the daisy wheel.
   */
  void save(uint32_t newAlignFactor, uint32_t newForceFactor);
};
