#pragma once

#include <stdint.h>

/**
 * Pure geometry math for the press, deliberately free of Arduino, ESP32Servo
 * and every other hardware dependency so it can be exercised on the host with
 * `pio test -e native` (see test/test_press_geometry).
 *
 * Keeping this arithmetic testable matters more than it looks: force 1 to 9
 * spans only PRESS_BITE_AT_MAX_FORCE degrees of servo rotation, which is a
 * millimetre or two at the pad. That is far too small to judge by watching the
 * arm move, so a bug here is invisible on the bench and can only be caught by
 * checking the numbers directly.
 */

/* constexpr rather than #define: these are typed, scoped to the compiler
 * rather than the preprocessor, and cannot silently rewrite an identically
 * named identifier in the Arduino or ESP32Servo headers. */

/** Lowest and highest calibration value the web UI can produce, for both the
 * align and the force fields. */
constexpr int CALIBRATION_VALUE_MIN = 1;
constexpr int CALIBRATION_VALUE_MAX = 9;

/** Mechanical travel limits of a standard hobby servo. */
constexpr int SERVO_ANGLE_MIN = 0;
constexpr int SERVO_ANGLE_MAX = 180;

/** Number of gaps between the nine selectable force values. */
constexpr int PRESS_FORCE_STEPS = CALIBRATION_VALUE_MAX - CALIBRATION_VALUE_MIN;

/**
 * @brief True when v is a calibration value the UI could actually have sent.
 *
 * Used at the HTTP boundary so an out-of-range value is refused outright.
 * Everything downstream clamps instead, and a clamp is silent: the panel
 * would report the save succeeded while the machine ran on a different
 * number.
 */
inline bool isValidCalibrationValue(int v) {
  return v >= CALIBRATION_VALUE_MIN && v <= CALIBRATION_VALUE_MAX;
}

/** @brief Pins an arbitrary integer into the 1-9 the UI offers. */
inline int clampCalibrationValue(int v) {
  if (v < CALIBRATION_VALUE_MIN) return CALIBRATION_VALUE_MIN;
  if (v > CALIBRATION_VALUE_MAX) return CALIBRATION_VALUE_MAX;
  return v;
}

/**
 * @brief Unsigned counterpart of clampCalibrationValue().
 *
 * Settings stores both factors as uint32_t. Narrowing a large unsigned to int
 * before clamping is implementation-defined behaviour before C++20, so the
 * comparison is done in the unsigned domain instead of casting at the call
 * site.
 */
inline uint32_t clampCalibrationValueUnsigned(uint32_t v) {
  constexpr uint32_t lowest = CALIBRATION_VALUE_MIN;
  constexpr uint32_t highest = CALIBRATION_VALUE_MAX;
  if (v < lowest) return lowest;
  if (v > highest) return highest;
  return v;
}

/**
 * @brief Which way the servo turns to drive the press into the daisy wheel:
 * +1 when the stamp angle is above rest, -1 when it is below.
 *
 * Stock geometry has rest HIGH and stamp LOW, but the P_press is press-fit
 * onto the splined hub, so a part seated a tooth out reverses the sense. This
 * is derived from the two taught angles rather than hardcoded because assuming
 * it drove the press away from the daisy wheel for an entire bench session.
 */
inline int pressDirection(int restAngle, int stampAngle) {
  return (stampAngle >= restAngle) ? 1 : -1;
}

/**
 * @brief Peak servo angle for a given force setting.
 *
 * Force 1 lands exactly on stampAngle -- the taught just-touching point, which
 * is the light press the alignment test is documented to make. Each further
 * step bites deeper, reaching biteAtMaxForce degrees past the touch point at
 * force 9. The result is always within the servo's travel.
 */
inline int pressPeakAngle(int restAngle, int stampAngle, int biteAtMaxForce,
                          int force) {
  force = clampCalibrationValue(force);
  if (biteAtMaxForce < 0) biteAtMaxForce = 0;

  const int dir = pressDirection(restAngle, stampAngle);

  // Rounding to nearest keeps the nine steps evenly spread over the travel
  // available. Plain truncation makes the first steps land on the same angle
  // whenever biteAtMaxForce is not a multiple of PRESS_FORCE_STEPS, which
  // silently removes the low end of the force range.
  const int span = force - CALIBRATION_VALUE_MIN;
  const int bite =
      (span * biteAtMaxForce + PRESS_FORCE_STEPS / 2) / PRESS_FORCE_STEPS;

  const int peak = stampAngle + dir * bite;
  if (peak < SERVO_ANGLE_MIN) return SERVO_ANGLE_MIN;
  if (peak > SERVO_ANGLE_MAX) return SERVO_ANGLE_MAX;
  return peak;
}
