#include "Settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include <tuple>

#include "Configuration.h"
#include "Logger.h"


Settings::Settings(Logger* logger) { this->logger = logger; }

Settings::~Settings() { delete this->preferences; }

void Settings::initialize() {
  // load settings from internal memory
  this->preferences->begin("calibration", false);

  // Ask whether the keys exist rather than reading them and treating 0 as
  // "never set". They are different questions, and conflating them means a
  // stored 0 -- which nothing should write, but which any bug upstream of
  // here could -- comes back as the default with nothing reporting it.
  if (!this->preferences->isKey("align")) {
    this->preferences->putUInt("align", DEFAULT_ALIGN_FACTOR);
  }
  if (!this->preferences->isKey("force")) {
    this->preferences->putUInt("force", DEFAULT_FORCE_FACTOR);
  }

  // Still clamped on the way out: a value that predates the range check at
  // the HTTP boundary would otherwise compute a peak angle below the taught
  // touch point and press nothing at all.
  this->alignFactor = clampCalibrationValueUnsigned(
      this->preferences->getUInt("align", DEFAULT_ALIGN_FACTOR));
  this->forceFactor = clampCalibrationValueUnsigned(
      this->preferences->getUInt("force", DEFAULT_FORCE_FACTOR));
  this->preferences->end();

  this->logger->log(String("Align factor: ") + this->alignFactor);
  this->logger->log(String("Force factor: ") + this->forceFactor);
}

void Settings::save(uint32_t newAlignFactor, uint32_t newForceFactor) {
  // save settings to memory

  // Last line of defence, and a noisy one. The HTTP handlers already reject
  // out-of-range values; anything that still gets here is a bug, so clamp to
  // keep the machine usable and say so rather than storing a number that
  // would compute a peak angle short of the daisy wheel.
  const uint32_t align = clampCalibrationValueUnsigned(newAlignFactor);
  const uint32_t force = clampCalibrationValueUnsigned(newForceFactor);
  if (align != newAlignFactor || force != newForceFactor) {
    this->logger->warn(String("Clamped calibration to align ") + align +
                       " force " + force);
  }

  this->preferences->begin("calibration", false);

  this->preferences->putUInt("align", align);
  this->preferences->putUInt("force", force);

  this->alignFactor = align;
  this->forceFactor = force;

  this->logger->log(String("Saved align ") + alignFactor);
  this->logger->log(String("Saved force ") + forceFactor);

  this->preferences->end();
}

uint32_t Settings::getAlignFactor() { return this->alignFactor; }

uint32_t Settings::getForceFactor() { return this->forceFactor; }
