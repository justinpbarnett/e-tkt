#include "Press.h"

#include <Arduino.h>

#include "Configuration.h"
#include "Drivers.h"
#include "Light.h"
#include "Logger.h"
#include "PressGeometry.h"

Press::Press(Logger* logger, uint8_t pin, Light* pressLed, ServoDriver* servo) {
  this->logger = logger;
  this->pin = pin;
  this->pressLed = pressLed;
  this->servo = servo;
}

Press::~Press() {
  // The servo is handed in, not built here, so it is not ours to delete.
}

void Press::initialize() {
  // set  servo
  this->servo->attach(this->pin);
  this->rest();
  delay(100);
}

void Press::sweep(int fromAngle, int toAngle, int stepMs) {
  const int dir = pressDirection(fromAngle, toAngle);
  // Counting the steps up front bounds the loop by distance rather than by
  // hitting an exact angle, so a clamped endpoint can never run it away.
  const int steps = abs(toAngle - fromAngle);
  // The starting angle is written too, matching the original loop: it is
  // normally a no-op because the servo is already there, but the bench hold()
  // path can leave it somewhere else, and it keeps the ramp's total duration
  // at (steps + 1) * stepMs.
  int pos = fromAngle;
  this->servo->write(pos);
  delay(stepMs);
  for (int i = 0; i < steps; i++) {
    pos += dir;
    this->servo->write(pos);
    delay(stepMs);
  }
}

void Press::settle(int angle, int holdMs) {
  for (int elapsed = 0; elapsed < holdMs; elapsed += PRESS_SETTLE_STEP_MS) {
    this->servo->write(angle);
    delay(PRESS_SETTLE_STEP_MS);
  }
}

void Press::press(bool strong, int force, bool slow) {
  if (!ENABLE_PRESS) {
    delay(500);
    return;
  }

  this->logger->log("Pressing...");

  const int stepMs = strong ? PRESS_STEP_STRONG_MS
                            : (slow ? PRESS_STEP_SLOW_MS : PRESS_STEP_QUICK_MS);

  // The press runs from REST_ANGLE to STAMP_ANGLE -- the measured
  // just-touching point -- and then further by an amount that scales with
  // force. The arithmetic lives in PressGeometry.h so it can be tested on the
  // host; see test/test_press_geometry. It also clamps force into 1-9, which
  // matters because Settings::getForceFactor() returns 0 when the EEPROM key
  // is missing and a raw 0 would compute a peak shallower than the touch point.
  const int peakAngle =
      pressPeakAngle(REST_ANGLE, STAMP_ANGLE, PRESS_BITE_AT_MAX_FORCE, force);
  this->logger->log(String("  force ") + clampCalibrationValue(force) +
                    " -> peak " + peakAngle + " deg");

  this->pressLed->on(LIGHT_FULL);  // lights up the char led

  this->sweep(REST_ANGLE, peakAngle, stepMs);

  const int dwellMs = slow ? PRESS_TEST_DWELL_MS : PRESS_DWELL_MS;
  if (slow) {
    this->logger->log(String("  HOLDING at ") + peakAngle + " deg for " +
                      dwellMs + "ms -- look at the gap now");
  }
  this->settle(peakAngle, dwellMs);

  this->sweep(peakAngle, REST_ANGLE, stepMs);
  this->settle(REST_ANGLE, PRESS_DWELL_MS);

  this->pressLed->on(LIGHT_DIM);  // dims the char led
}

void Press::rest() { this->servo->write(REST_ANGLE); }

void Press::hold(int angle) { this->servo->write(angle); }

void Press::release() { this->servo->detach(); }

void Press::engage() { this->servo->attach(this->pin); }
