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

inline bool operator==(const Run& a, const Run& b) {
  return a.label == b.label && a.copies == b.copies && a.cut == b.cut;
}

/**
 * @brief Keeps the last run the machine printed, and how far it got, in
 * EEPROM, so the button on the machine can print it: after a reboot, and
 * with no phone and no network to ask it over.
 *
 * The command loop keeps a run as it starts one and as it ends, and the
 * button reads it from a task of its own, so every access is under one lock,
 * which also covers Preferences, as Roll's does.
 */
class LastRun {
 private:
  Logger* logger;
  // By value, for the reason Settings gives.
  Preferences preferences;
  std::mutex lock;
  Run current;
  // How many of its labels are printed and not to be printed again. See
  // keep().
  int printed = 0;
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
   * @brief Keeps a run as the last one, with how many of its labels are
   * printed.
   *
   * The button prints the labels after those. So `printed` is 0 for a run
   * that is to come whole, which is one that ran to its end as much as one
   * that got no label done, and it is always less than the run's copies.
   *
   * Written only when the run or the count differs from what is already
   * kept, so printing the same run again and again, which is what the button
   * is for, writes nothing.
   */
  void keep(const Run& run, int printed);

  /**
   * @brief Copies the last run into `run`, and how many of its labels are
   * printed into `printed`. Returns false, and leaves both as they were,
   * when there is none.
   */
  bool read(Run* run, int* printed);
};
