#include "Logger.h"

#include <Arduino.h>

#include <cstring>
#include <mutex>

#include "Configuration.h"

// Seconds since boot to three places, e.g. "12.345". Built by hand because
// Arduino's String has no format call and a float would round the tail off
// exactly where the interesting part is.
static String bootTimestamp() {
  const unsigned long ms = millis();
  const unsigned long frac = ms % 1000;
  String stamp = String(ms / 1000) + ".";
  if (frac < 100) {
    stamp += "0";
  }
  if (frac < 10) {
    stamp += "0";
  }
  stamp += String(frac);
  return stamp;
}

// Says that a trail holds lines, and is not what the memory came up as when
// the power came on.
static const uint32_t TRAIL_MARK = 0x4C4F4721;

// The lines of the trail, oldest first, as recent() serves them. Read from a
// trail a restart left, every number in it is checked before it is believed.
static String linesOf(LogTrail* trail) {
  if (trail->next >= LOG_HISTORY_LINES || trail->stored > LOG_HISTORY_LINES) {
    return "";
  }
  // Once the buffer has wrapped, the oldest line is the one next is about to
  // overwrite; before that it is slot zero.
  const size_t oldest = (trail->stored < LOG_HISTORY_LINES) ? 0 : trail->next;
  String out = "";
  for (size_t i = 0; i < trail->stored; i++) {
    char* line = trail->lines[(oldest + i) % LOG_HISTORY_LINES];
    // A line a restart cut off as it was being written still ends.
    line[LOG_LINE_BYTES - 1] = '\0';
    if (i > 0) {
      out += "\n";
    }
    out += line;
  }
  return out;
}

static void clearTrail(LogTrail* trail) {
  trail->next = 0;
  trail->stored = 0;
  trail->mark = TRAIL_MARK;
}

Logger::Logger() {
  this->trail = new LogTrail();
  this->ownsTrail = true;
  clearTrail(this->trail);
}

Logger::Logger(LogTrail* trail) {
  this->trail = trail;
  this->ownsTrail = false;
  if (trail->mark == TRAIL_MARK) {
    this->before = linesOf(trail);
  }
  clearTrail(trail);
}

Logger::~Logger() {
  if (this->ownsTrail) {
    delete this->trail;
  }
}

void Logger::initialize() {
  if (ENABLE_SERIAL) {
    Serial.begin(115200);

    // Log a nifty logo.
    Serial.println("");
    Serial.println(" _____    _____  _  __ _____  ");
    Serial.println("/  __/   /__ __\\/ |/ //__ __\\ ");
    Serial.println("|  \\ _____ / \\  |   /   / \\   ");
    Serial.println("|  /_\\____\\| |  |   \\   | |   ");
    Serial.println("\\____\\     \\_/  \\_|\\_\\  \\_/   ");
    Serial.println("");
    Serial.println("Anachronistic label maker designed by");
    Serial.println("https://andrei.cc and made by you!");
    Serial.println("");
  }
}

void Logger::record(const char* level, const char* serialPrefix,
                    const String& message) {
  if (ENABLE_SERIAL) {
    Serial.println(String(serialPrefix) + message);
  }

  const String line = bootTimestamp() + " " + level + " " + message;

  std::lock_guard<std::mutex> held(this->lock);
  char* slot = this->trail->lines[this->trail->next];
  strncpy(slot, line.c_str(), LOG_LINE_BYTES - 1);
  slot[LOG_LINE_BYTES - 1] = '\0';
  this->trail->next = (this->trail->next + 1) % LOG_HISTORY_LINES;
  if (this->trail->stored < LOG_HISTORY_LINES) {
    this->trail->stored++;
  }
}

void Logger::log(const String& message) { this->record("INFO ", "", message); }

void Logger::warn(const String& message) {
  this->record("WARN ", "WARN  ", message);
}

void Logger::error(const String& message) {
  this->record("ERROR", "ERROR ", message);
}

String Logger::recent() {
  std::lock_guard<std::mutex> held(this->lock);
  return linesOf(this->trail);
}

String Logger::beforeRestart() { return this->before; }

void Logger::startedAfter(const String& cause) {
  this->startCause = cause;
  this->log(String("started after ") + cause);
}

String Logger::startedAfter() { return this->startCause; }
