#include "LastRun.h"

#include <Arduino.h>
#include <Preferences.h>

#include <mutex>
#include <vector>

#include "ArduinoJson.h"
#include "Logger.h"
#include "Tape.h"

// Where the run lives in EEPROM, as one entry: the label, how many of it and
// the cut, written together as JSON. An entry each would be three writes,
// and a power cut between them would leave the new label with the old count,
// for the button to print.
static const char* LAST_RUN_NAMESPACE = "lastrun";
static const char* RUN_KEY = "run";

// The run as it is stored.
static String stored(const Run& run) {
  StaticJsonDocument<JSON_OBJECT_SIZE(3)> doc;
  doc["label"] = run.label.c_str();
  doc["copies"] = run.copies;
  doc["cut"] = run.cut;
  std::vector<char> text(measureJson(doc) + 1);
  serializeJson(doc, text.data(), text.size());
  return String(text.data());
}

// Reads a stored run into `run`. Returns false, and leaves `run` as it was,
// for anything that is not a run as stored() writes one: the button starts
// what is read here with nobody having typed it, so a run that is only
// partly understood is not guessed at.
static bool readStored(const String& text, Run* run) {
  // Every string in the text is copied into the document, and none is longer
  // there than it was in the text.
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(3) + text.length());
  if (deserializeJson(doc, text.c_str(), text.length())) {
    return false;
  }
  if (!doc["label"].is<const char*>() || !doc["copies"].is<int>() ||
      !doc["cut"].is<bool>()) {
    return false;
  }
  if (!isValidCopies(doc["copies"].as<int>())) {
    return false;
  }
  run->label = String(doc["label"].as<const char*>());
  run->copies = doc["copies"].as<int>();
  run->cut = doc["cut"].as<bool>();
  return true;
}

LastRun::LastRun(Logger* logger) { this->logger = logger; }

void LastRun::initialize() {
  this->lock.lock();
  this->preferences.begin(LAST_RUN_NAMESPACE, false);
  // A missing key is a device that has printed no run -- asked rather than
  // inferred from an empty text, as Settings does.
  const bool found = this->preferences.isKey(RUN_KEY);
  const String text =
      found ? this->preferences.getString(RUN_KEY, "") : String("");
  this->preferences.end();

  this->kept = found && readStored(text, &this->current);
  const bool kept = this->kept;
  const Run loaded = this->current;
  this->lock.unlock();

  if (found && !kept) {
    this->logger->warn("Ignored a stored last run that cannot be read");
  }
  if (kept) {
    this->logger->log(String("Last run: ") + loaded.label + " x " +
                      loaded.copies);
  }
}

void LastRun::keep(const Run& run) {
  this->lock.lock();
  const bool same = this->kept && this->current.label == run.label &&
                    this->current.copies == run.copies &&
                    this->current.cut == run.cut;
  if (!same) {
    this->current = run;
    this->kept = true;
    this->preferences.begin(LAST_RUN_NAMESPACE, false);
    this->preferences.putString(RUN_KEY, stored(run).c_str());
    this->preferences.end();
  }
  this->lock.unlock();
}

bool LastRun::read(Run* run) {
  this->lock.lock();
  const bool kept = this->kept;
  if (kept) {
    *run = this->current;
  }
  this->lock.unlock();
  return kept;
}
