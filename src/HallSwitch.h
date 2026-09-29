#pragma once

#include <Arduino.h>

#include "Configuration.h"
#include "Logger.h"

/**
 * @brief Controls a hall effect sensor.
 *
 * The hall effect sensor is used to detect when the daisy wheel has rotated
 * to the home position.
 */
class HallSwitch {
 private:
  Logger* logger;
  uint8_t pin;

 public:
  HallSwitch(Logger* logger, uint8_t pin);
  ~HallSwitch();
  void initialize();

  /**
   * @brief Whether the magnet is in front of the sensor: a reading below
   * HALL_SENSOR_THRESHOLD, or above it with INVERT_HALL_SENSOR_LOGIC set.
   */
  bool triggered();

  /**
   * @brief The raw analog reading, 0 to 4095. HALL_SENSOR_THRESHOLD is taught
   * from it, with the hall bench rig in src/bench/hall.cpp.
   */
  int reading();
};
