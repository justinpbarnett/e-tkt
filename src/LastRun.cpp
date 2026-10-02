#include "LastRun.h"

#include <Arduino.h>
#include <Preferences.h>

#include <mutex>

#include "ArduinoJson.h"
#include "JsonText.h"
#include "Logger.h"
#include "Tape.h"

// Where the run lives in EEPROM, as one entry: the label, how many of it,
// the cut, and how many of them are printed, written together as JSON. An
// entry each would be four writes, and a power cut between them would leave
// the new label with the old count, for the button to print.
static const char* LAST_RUN_NAMESPACE = "lastrun";
static const char* RUN_KEY = "run";

// The run as it is stored, with how many of its labels are printed.
static String stored(const Run& run, int printed) {
  StaticJsonDocument<JSON_OBJECT_SIZE(4)> doc;
  doc["label"] = run.label.c_str();
  doc["copies"] = run.copies;
  doc["cut"] = run.cut;
  doc["printed"] = printed;
  return jsonText(doc);
}

// Reads a stored run into `run`, and how many of its labels are printed into
// `printed`. Returns false, and leaves both as they were, for anything that
// is not a run as stored() writes one: the button starts what is read here
// with nobody having typed it, so a run that is only partly understood is
// not guessed at.
static bool readStored(const String& text, Run* run, int* printed) {
  // A text that is not JSON reads as a document with nothing in it, and so
  // with no label.
  const DynamicJsonDocument doc = parsedJson(text, JSON_OBJECT_SIZE(4));
  if (!doc["label"].is<const char*>() || !doc["copies"].is<int>() ||
      !doc["cut"].is<bool>()) {
    return false;
  }
  const int copies = doc["copies"].as<int>();
  if (!isValidCopies(copies)) {
    return false;
  }
  // The firmware before this one stored no count, and printed the whole run
  // again. So does this one, for a run that firmware stored.
  int done = 0;
  if (doc.containsKey("printed")) {
    if (!doc["printed"].is<int>()) {
      return false;
    }
    done = doc["printed"].as<int>();
    if (done < 0 || done >= copies) {
      return false;
    }
  }
  run->label = String(doc["label"].as<const char*>());
  run->copies = copies;
  run->cut = doc["cut"].as<bool>();
  *printed = done;
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

  this->kept = found && readStored(text, &this->current, &this->printed);
  const bool kept = this->kept;
  const Run loaded = this->current;
  const int printed = this->printed;
  this->lock.unlock();

  if (found && !kept) {
    this->logger->warn("Ignored a stored last run that cannot be read");
  }
  if (kept) {
    String line = String("Last run: ") + loaded.label + " x " + loaded.copies;
    if (printed > 0) {
      line += String(", ") + printed + " printed";
    }
    this->logger->log(line);
  }
}

void LastRun::keep(const Run& run, int printed) {
  this->lock.lock();
  const bool same =
      this->kept && this->current == run && this->printed == printed;
  if (!same) {
    this->current = run;
    this->printed = printed;
    this->kept = true;
    this->preferences.begin(LAST_RUN_NAMESPACE, false);
    this->preferences.putString(RUN_KEY, stored(run, printed).c_str());
    this->preferences.end();
  }
  this->lock.unlock();
}

bool LastRun::read(Run* run, int* printed) {
  this->lock.lock();
  const bool kept = this->kept;
  if (kept) {
    *run = this->current;
    *printed = this->printed;
  }
  this->lock.unlock();
  return kept;
}
