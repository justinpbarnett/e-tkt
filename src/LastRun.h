#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <mutex>

#include "Logger.h"

/**
 * @brief A run of labels as it was asked for: the label, how many of it, and
 * whether each one is cut.
 */
struct Run {
  String label = "";
  int copies = 1;
  bool cut = true;
};

/**
 * @brief Keeps the last run the machine printed, in EEPROM, so the button on
 * the machine can print it again: after a reboot, and with no phone and no
 * network to ask it over.
 *
 * The command loop keeps a run as it starts one, and the button reads it from
 * a task of its own, so every access is under one lock, which also covers
 * Preferences, as Roll's does.
 */
class LastRun {
 private:
  Logger* logger;
  // By value, for the reason Settings gives.
  Preferences preferences;
  std::mutex lock;
  Run current;
  // Whether there is one. A label of nothing is a run too, so this is kept
  // rather than read off the label.
  bool kept = false;

 public:
  LastRun(Logger* logger);

  /**
   * @brief Reads the run back from EEPROM. A device that has never printed
   * one has none, and nor has one whose stored run cannot be read.
   */
  void initialize();

  /**
   * @brief Keeps a run as the last one.
   *
   * Written only when it differs from the one already kept, so printing the
   * same run again and again, which is what the button is for, writes
   * nothing.
   */
  void keep(const Run& run);

  /**
   * @brief Copies the last run into `run`. Returns false, and leaves `run`
   * as it was, when there is none.
   */
  bool read(Run* run);
};
