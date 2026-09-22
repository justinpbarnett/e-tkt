#pragma once

#include <Arduino.h>

#include <mutex>

#include "Configuration.h"

/**
 * How many lines the device keeps in RAM for recent(). Roughly a full print
 * plus the homing before it, which is as far back as anyone looks when asking
 * what just went wrong.
 */
#define LOG_HISTORY_LINES 32

/**
 * @brief Logs messages to the serial port, and keeps the last few in RAM.
 *
 * Two jobs, and the second is the one worth having. A machine on a bench with
 * no cable in it used to be unable to say anything at all about what it had
 * just done: ENABLE_SERIAL either sent everything down a USB port somebody had
 * to be watching, or dropped it. recent() holds the last LOG_HISTORY_LINES
 * either way, timestamped, so Network can serve them to a browser on the
 * same wifi.
 *
 * The buffer deliberately does not depend on ENABLE_SERIAL. Turning the serial
 * port off is a statement about the cable, not about whether the machine
 * should remember what it did.
 *
 * Safe to call from any task. The print job and the HTTP handlers log from
 * different threads, and a String assigned in one while being read in the
 * other is a crash rather than a garbled line.
 */
class Logger {
 private:
  std::mutex lock;
  String history[LOG_HISTORY_LINES];
  // Where the next line goes, and how many slots are filled. Once stored
  // reaches LOG_HISTORY_LINES the write index wraps and the oldest line is
  // the one it is about to overwrite.
  size_t next = 0;
  size_t stored = 0;

  /** @brief Timestamps the line, files it, and prints it if anyone is
   * listening. serialPrefix is what serial sees ahead of the message; the
   * empty string keeps a plain log() line byte for byte what it always was. */
  void record(const char* level, const char* serialPrefix,
              const String& message);

 public:
  void initialize();

  /** @brief Ordinary progress. Reaches serial with no decoration. */
  void log(String message);

  /** @brief Something is off but the machine carried on. */
  void warn(String message);

  /** @brief Something failed. */
  void error(String message);

  /**
   * @brief The last LOG_HISTORY_LINES lines, oldest first, newline separated
   * and each stamped with seconds since boot. Empty before anything is
   * logged, and never ends in a trailing newline.
   */
  String recent();
};
