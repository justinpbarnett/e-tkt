// Host-side tests for the pure press geometry math.
//
// Everything under test here is plain arithmetic with no Arduino, no servo and
// no hardware, which is the whole reason it was pulled out of Press::press().
// Run with:  pio test -e native
#include <stdint.h>
#include <unity.h>

#include "PressGeometry.h"

// Stock E-TKT geometry: rest is HIGH, stamp is LOW, so the press travels
// downward in angle as it moves into the daisy wheel.
static const int REST = 50;
static const int STAMP = 15;
static const int BITE = 8;

void setUp(void) {}
void tearDown(void) {}

// --- force clamping ------------------------------------------------------
// Settings::getForceFactor() returns 0 when the EEPROM key is missing. A raw 0
// used to compute a peak *shallower* than the touch point, i.e. a press that
// could never emboss. 0 must behave as the minimum the UI can actually pick.

void test_force_zero_is_treated_as_one(void) {
  TEST_ASSERT_EQUAL_INT(pressPeakAngle(REST, STAMP, BITE, 1),
                        pressPeakAngle(REST, STAMP, BITE, 0));
}

void test_negative_force_is_treated_as_one(void) {
  TEST_ASSERT_EQUAL_INT(pressPeakAngle(REST, STAMP, BITE, 1),
                        pressPeakAngle(REST, STAMP, BITE, -5));
}

void test_force_above_nine_is_treated_as_nine(void) {
  TEST_ASSERT_EQUAL_INT(pressPeakAngle(REST, STAMP, BITE, 9),
                        pressPeakAngle(REST, STAMP, BITE, 99));
}

// --- the two endpoints ---------------------------------------------------

void test_force_one_only_kisses_the_wheel(void) {
  // Force 1 is the documented "slowly and lightly" alignment press: it must
  // land exactly on the taught touch point, biting no further.
  TEST_ASSERT_EQUAL_INT(STAMP, pressPeakAngle(REST, STAMP, BITE, 1));
}

void test_force_nine_bites_the_full_configured_amount(void) {
  TEST_ASSERT_EQUAL_INT(STAMP - BITE, pressPeakAngle(REST, STAMP, BITE, 9));
}

// --- monotonicity --------------------------------------------------------
// The user cannot see 8 degrees of rotation on the arm, so this is the only
// place the "does force 1-9 actually increase travel" question gets answered.

void test_every_force_step_travels_at_least_as_far_as_the_last(void) {
  for (int f = 2; f <= 9; f++) {
    const int prev = pressPeakAngle(REST, STAMP, BITE, f - 1);
    const int curr = pressPeakAngle(REST, STAMP, BITE, f);
    TEST_ASSERT_TRUE_MESSAGE(curr <= prev, "peak angle must not move backwards");
  }
}

void test_all_nine_steps_are_distinct_when_travel_allows(void) {
  // BITE == 8 gives exactly one degree per step, so no two forces may collide.
  for (int f = 2; f <= 9; f++) {
    TEST_ASSERT_NOT_EQUAL(pressPeakAngle(REST, STAMP, BITE, f - 1),
                          pressPeakAngle(REST, STAMP, BITE, f));
  }
}

void test_steps_spread_evenly_when_bite_is_not_a_multiple_of_eight(void) {
  // Truncating division makes the first steps land on the same angle whenever
  // the travel is not divisible by 8. Rounding to nearest spreads them out.
  const int b = 5;
  TEST_ASSERT_EQUAL_INT(STAMP - 0, pressPeakAngle(REST, STAMP, b, 1));
  TEST_ASSERT_EQUAL_INT(STAMP - 1, pressPeakAngle(REST, STAMP, b, 2));
  TEST_ASSERT_EQUAL_INT(STAMP - 3, pressPeakAngle(REST, STAMP, b, 6));
  TEST_ASSERT_EQUAL_INT(STAMP - 5, pressPeakAngle(REST, STAMP, b, 9));
}

// --- direction -----------------------------------------------------------
// Which way "into the wheel" is depends on how the P_press was pushed onto the
// spline. Getting this backwards drove the press away from the wheel for a
// whole bench session, so it is derived, never assumed.

void test_inverted_geometry_bites_upward(void) {
  // rest LOW, stamp HIGH: the press travels up in angle to reach the wheel.
  TEST_ASSERT_EQUAL_INT(72, pressPeakAngle(4, 72, BITE, 1));
  TEST_ASSERT_EQUAL_INT(80, pressPeakAngle(4, 72, BITE, 9));
}

void test_direction_is_derived_from_the_two_taught_angles(void) {
  TEST_ASSERT_EQUAL_INT(-1, pressDirection(50, 15));
  TEST_ASSERT_EQUAL_INT(1, pressDirection(4, 72));
}

// --- servo travel limits -------------------------------------------------

void test_peak_never_exceeds_servo_range(void) {
  TEST_ASSERT_EQUAL_INT(0, pressPeakAngle(50, 2, 40, 9));
  TEST_ASSERT_EQUAL_INT(180, pressPeakAngle(100, 178, 40, 9));
}

// --- calibration value validation ----------------------------------------
// align and force are both 1-9 in the UI, and a POST of 0 used to reach
// EEPROM unchallenged. Network.cpp refuses out-of-range values outright
// rather than clamping, because a clamp is silent: the panel would report the
// save succeeded while the machine ran on a different number.

void test_valid_calibration_values_are_one_through_nine(void) {
  for (int v = 1; v <= 9; v++) {
    TEST_ASSERT_TRUE(isValidCalibrationValue(v));
  }
}

void test_zero_is_not_a_valid_calibration_value(void) {
  TEST_ASSERT_FALSE(isValidCalibrationValue(0));
}

void test_out_of_range_calibration_values_are_rejected(void) {
  TEST_ASSERT_FALSE(isValidCalibrationValue(-1));
  TEST_ASSERT_FALSE(isValidCalibrationValue(10));
  TEST_ASSERT_FALSE(isValidCalibrationValue(1000));
}

void test_clamping_pins_to_the_usable_range(void) {
  TEST_ASSERT_EQUAL_INT(1, clampCalibrationValue(0));
  TEST_ASSERT_EQUAL_INT(1, clampCalibrationValue(-7));
  TEST_ASSERT_EQUAL_INT(9, clampCalibrationValue(10));
  TEST_ASSERT_EQUAL_INT(5, clampCalibrationValue(5));
}

// Settings stores align and force as uint32_t. Narrowing a large unsigned to
// int before clamping is implementation-defined before C++20, so the unsigned
// path gets its own entry point rather than a cast at the call site.

void test_unsigned_clamp_matches_signed_clamp_in_range(void) {
  for (uint32_t v = 1; v <= 9; v++) {
    TEST_ASSERT_EQUAL_UINT32(v, clampCalibrationValueUnsigned(v));
  }
}

void test_unsigned_clamp_pins_zero_to_the_minimum(void) {
  TEST_ASSERT_EQUAL_UINT32(1, clampCalibrationValueUnsigned(0));
}

void test_unsigned_clamp_pins_large_values_to_the_maximum(void) {
  TEST_ASSERT_EQUAL_UINT32(9, clampCalibrationValueUnsigned(10));
  TEST_ASSERT_EQUAL_UINT32(9, clampCalibrationValueUnsigned(4294967295u));
}

// Pins the peak angles actually observed on machine 1's serial log on
// 2026-09-22, with REST_ANGLE 50, STAMP_ANGLE 15, PRESS_BITE_AT_MAX_FORCE 8.
// The extraction of this arithmetic out of Press::press() must not move them.
void test_matches_angles_measured_on_hardware(void) {
  TEST_ASSERT_EQUAL_INT(15, pressPeakAngle(50, 15, 8, 1));
  TEST_ASSERT_EQUAL_INT(13, pressPeakAngle(50, 15, 8, 3));
  TEST_ASSERT_EQUAL_INT(11, pressPeakAngle(50, 15, 8, 5));
  TEST_ASSERT_EQUAL_INT(9, pressPeakAngle(50, 15, 8, 7));
  TEST_ASSERT_EQUAL_INT(7, pressPeakAngle(50, 15, 8, 9));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_force_zero_is_treated_as_one);
  RUN_TEST(test_negative_force_is_treated_as_one);
  RUN_TEST(test_force_above_nine_is_treated_as_nine);
  RUN_TEST(test_force_one_only_kisses_the_wheel);
  RUN_TEST(test_force_nine_bites_the_full_configured_amount);
  RUN_TEST(test_every_force_step_travels_at_least_as_far_as_the_last);
  RUN_TEST(test_all_nine_steps_are_distinct_when_travel_allows);
  RUN_TEST(test_steps_spread_evenly_when_bite_is_not_a_multiple_of_eight);
  RUN_TEST(test_inverted_geometry_bites_upward);
  RUN_TEST(test_direction_is_derived_from_the_two_taught_angles);
  RUN_TEST(test_peak_never_exceeds_servo_range);
  RUN_TEST(test_valid_calibration_values_are_one_through_nine);
  RUN_TEST(test_zero_is_not_a_valid_calibration_value);
  RUN_TEST(test_out_of_range_calibration_values_are_rejected);
  RUN_TEST(test_clamping_pins_to_the_usable_range);
  RUN_TEST(test_unsigned_clamp_matches_signed_clamp_in_range);
  RUN_TEST(test_unsigned_clamp_pins_zero_to_the_minimum);
  RUN_TEST(test_unsigned_clamp_pins_large_values_to_the_maximum);
  RUN_TEST(test_matches_angles_measured_on_hardware);
  return UNITY_END();
}
