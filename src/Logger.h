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
 * How long a kept line can be, with the NUL at its end. A longer one is cut
 * short in recent(), never on serial.
 */
#define LOG_LINE_BYTES 128

/**
 * @brief The lines recent() serves, in memory a restart can leave alone.
 *
 * A machine that restarts in the middle of a job used to take the lines that
 * said why with it, and those are the ones anybody needs. On the board this
 * lives where the chip keeps what it holds through every restart that leaves
 * the power on: a crash, a watchdog, a dip in the supply that does not take
 * it all the way down. The next start reads what is there before it logs a
 * line of its own. A power cut leaves noise, which `mark` tells from lines.
 */
struct LogTrail {
  uint32_t mark;
  // Where the next line goes, and how many slots are filled. Once stored
  // reaches LOG_HISTORY_LINES the write index wraps and the oldest line is
  // the one it is about to overwrite.
  uint32_t next;
  uint32_t stored;
  char lines[LOG_HISTORY_LINES][LOG_LINE_BYTES];
};

/**
 * @brief Logs messages to the serial port, and keeps the last few in RAM.
 *
 * Two jobs, and the second is the one worth having. A machine on a bench with
 * no cable in it used to be unable to say anything at all about what it had
 * just done: ENABLE_SERIAL either sent everything down a USB port somebody had
 * to be watching, or dropped it. recent() holds the last LOG_HISTORY_LINES
 * either way, timestamped, so the Api can serve them to a browser on the
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
  LogTrail* trail;
  bool ownsTrail;
  // What the trail held as this start began, as recent() would have said it,
  // and why the machine started.
  String before = "";
  String startCause = "";

  /** @brief Timestamps the line, files it, and prints it if anyone is
   * listening. serialPrefix is what serial sees ahead of the message; the
   * empty string keeps a plain log() line byte for byte what it always was. */
  void record(const char* level, const char* serialPrefix,
              const String& message);

 public:
  /** @brief Keeps its lines in memory of its own, which starts empty. */
  Logger();

  /**
   * @brief Keeps its lines in `trail`, which may hold the lines of the start
   * before this one. Those are read before anything else is written there,
   * and beforeRestart() has them from then on.
   */
  explicit Logger(LogTrail* trail);

  ~Logger();

  void initialize();

  /** @brief Ordinary progress. Reaches serial with no decoration. */
  void log(const String& message);

  /** @brief Something is off but the machine carried on. */
  void warn(const String& message);

  /** @brief Something failed. */
  void error(const String& message);

  /**
   * @brief The last LOG_HISTORY_LINES lines, oldest first, newline separated
   * and each stamped with seconds since boot. Empty before anything is
   * logged, and never ends in a trailing newline.
   */
  String recent();

  /**
   * @brief The last lines of the start before this one, as recent() said
   * them, which is what was going on as the machine restarted. Empty when
   * the power was off in between, and when nothing was logged.
   */
  String beforeRestart();

  /**
   * @brief Notes why the machine started: "power on", "crash", "task
   * watchdog". Said once, as the start begins, and logged.
   */
  void startedAfter(const String& cause);

  /** @brief What startedAfter() was told. Empty until then. */
  String startedAfter();
};
