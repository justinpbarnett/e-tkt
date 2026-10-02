#include "Roll.h"

#include <Arduino.h>
#include <Preferences.h>

#include <mutex>

#include "Configuration.h"
#include "Logger.h"
#include "Tape.h"

// Where the count lives in EEPROM. "used" is stored as feeds rather than
// millimetres: feeds are what the machine actually knows, and storing tape
// would bake today's FEED_LENGTH_UM into every roll already counted.
static const char* ROLL_NAMESPACE = "roll";
static const char* LENGTH_KEY = "length";
static const char* USED_KEY = "used";
static const char* OUT_KEY = "out";

Roll::Roll(Logger* logger) { this->logger = logger; }

void Roll::initialize() {
  this->lock.lock();
  this->preferences.begin(ROLL_NAMESPACE, false);

  // A missing key is a device that has never counted a roll, not a roll of
  // no length -- asked rather than inferred from a zero, as Settings does.
  if (!this->preferences.isKey(LENGTH_KEY)) {
    this->preferences.putUInt(LENGTH_KEY, DEFAULT_ROLL_LENGTH_MM);
  }
  if (!this->preferences.isKey(USED_KEY)) {
    this->preferences.putUInt(USED_KEY, 0);
  }
  // Nor is it one with its roll out: every machine that was built before
  // this was kept has a roll in it, as far as anyone can tell.
  if (!this->preferences.isKey(OUT_KEY)) {
    this->preferences.putBool(OUT_KEY, false);
  }
  const uint32_t stored =
      this->preferences.getUInt(LENGTH_KEY, DEFAULT_ROLL_LENGTH_MM);
  this->current.feedsUsed = this->preferences.getUInt(USED_KEY, 0);
  this->current.out = this->preferences.getBool(OUT_KEY, false);
  this->preferences.end();

  // Only load() writes the length, and it clamps, so this is a value from
  // some other firmware rather than one this code stored.
  this->current.lengthMm =
      isValidRollLength(stored) ? stored : DEFAULT_ROLL_LENGTH_MM;
  const RollState loaded = this->current;
  this->lock.unlock();

  if (loaded.lengthMm != stored) {
    this->logger->warn(String("Ignored a stored roll length of ") + stored +
                       " mm");
  }
  this->logger->log(String("Roll: ") + loaded.lengthMm + " mm, " +
                    loaded.feedsUsed + " feeds used" +
                    (loaded.out ? ", out of the machine" : ""));
}

void Roll::load(uint32_t lengthMm) {
  uint32_t length = lengthMm;
  if (length < (uint32_t)ROLL_LENGTH_MIN_MM) {
    length = ROLL_LENGTH_MIN_MM;
  } else if (length > (uint32_t)ROLL_LENGTH_MAX_MM) {
    length = ROLL_LENGTH_MAX_MM;
  }

  this->lock.lock();
  this->current.lengthMm = length;
  this->current.feedsUsed = 0;
  this->preferences.begin(ROLL_NAMESPACE, false);
  this->preferences.putUInt(LENGTH_KEY, this->current.lengthMm);
  this->preferences.putUInt(USED_KEY, this->current.feedsUsed);
  this->preferences.end();
  this->lock.unlock();

  if (length != lengthMm) {
    this->logger->warn(String("Clamped roll length to ") + length + " mm");
  }
  this->logger->log(String("New roll: ") + length + " mm");
}

void Roll::use(uint32_t feeds) {
  if (feeds == 0) {
    return;
  }
  // Written through on every call, which the command loop makes once a
  // label. That is a few hundred small writes a roll, well inside what the
  // NVS partition's wear levelling is built for, and it means a power cut
  // mid-batch loses at most the label that was being pressed.
  this->lock.lock();
  this->current.feedsUsed += feeds;
  this->preferences.begin(ROLL_NAMESPACE, false);
  this->preferences.putUInt(USED_KEY, this->current.feedsUsed);
  this->preferences.end();
  this->lock.unlock();
}

void Roll::takeOut() { this->markOut(true); }

void Roll::putIn() { this->markOut(false); }

void Roll::markOut(bool out) {
  this->lock.lock();
  const bool changed = this->current.out != out;
  if (changed) {
    this->current.out = out;
    this->preferences.begin(ROLL_NAMESPACE, false);
    this->preferences.putBool(OUT_KEY, out);
    this->preferences.end();
  }
  this->lock.unlock();

  if (changed) {
    this->logger->log(out ? "Roll out of the machine" : "Roll in the machine");
  }
}

RollState Roll::state() {
  this->lock.lock();
  const RollState copy = this->current;
  this->lock.unlock();
  return copy;
}
