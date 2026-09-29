#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <mutex>

#include "Configuration.h"
#include "Logger.h"

/**
 * @brief What is known about the roll of tape in the machine.
 *
 * Two numbers, and neither is measured: the length the roll was declared at
 * when it went in, and how many feeds the machine has taken from it since.
 * What that leaves is worked out from the two in Tape.h, so the rounding
 * lives in one place.
 */
struct RollState {
  uint32_t lengthMm = DEFAULT_ROLL_LENGTH_MM;
  uint32_t feedsUsed = 0;
};

/**
 * @brief Keeps the count of tape used from the roll, in EEPROM, so a reboot
 * does not refill the roll.
 *
 * The command loop adds to the count as it feeds, and the webserver reads it
 * on every status poll. Those run on different FreeRTOS tasks, so every
 * access is under one lock -- which also covers Preferences, since that is
 * not safe to use from two tasks at once either.
 */
class Roll {
 private:
  Logger* logger;
  // By value, for the reason Settings gives.
  Preferences preferences;
  std::mutex lock;
  RollState current;

 public:
  Roll(Logger* logger);

  /**
   * @brief Reads the count back from EEPROM. A device that has never kept
   * one starts on a full DEFAULT_ROLL_LENGTH_MM roll.
   */
  void initialize();

  /**
   * @brief Starts the count again for a new roll of the given length.
   *
   * The length is clamped into ROLL_LENGTH_MIN_MM..ROLL_LENGTH_MAX_MM and the
   * clamp is logged. The HTTP handler refuses anything outside that range, so
   * a clamp here is a bug upstream, not a user's typo.
   */
  void load(uint32_t lengthMm);

  /**
   * @brief Counts feeds taken from the roll.
   */
  void use(uint32_t feeds);

  /**
   * @brief A copy of the count, consistent with itself.
   */
  RollState state();
};
