#include "Logger.h"

#include <Arduino.h>

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
  this->history[this->next] = line;
  this->next = (this->next + 1) % LOG_HISTORY_LINES;
  if (this->stored < LOG_HISTORY_LINES) {
    this->stored++;
  }
}

void Logger::log(String message) { this->record("INFO ", "", message); }

void Logger::warn(String message) { this->record("WARN ", "WARN  ", message); }

void Logger::error(String message) { this->record("ERROR", "ERROR ", message); }

String Logger::recent() {
  std::lock_guard<std::mutex> held(this->lock);

  // Oldest first. Once the buffer has wrapped, the oldest line is the one
  // next is about to overwrite; before that it is slot zero.
  const size_t oldest = (this->stored < LOG_HISTORY_LINES) ? 0 : this->next;

  String out = "";
  for (size_t i = 0; i < this->stored; i++) {
    if (i > 0) {
      out += "\n";
    }
    out += this->history[(oldest + i) % LOG_HISTORY_LINES];
  }
  return out;
}
