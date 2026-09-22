// Host-side tests for Press, driven through the ServoDriver seam.
//
// test_press_geometry already pins the arithmetic. This file asks the next
// question, the one the arithmetic cannot answer: does Press actually drive
// the servo to the angle the arithmetic worked out, and how long does it hold
// it there? That is where the bench trouble on machine 1 lived -- the numbers
// were right and the stroke still looked unchanged -- and until the driver
// became a constructor parameter there was no way to look.
//
// A FakeServo records every attach, detach and write against the virtual
// clock in test/stubs/Arduino.h, so "held at peak for 250ms" is an assertion
// rather than a reading off a scope. Run with:  pio test -e native
#include <unity.h>

#include "Arduino.h"
#include "Configuration.h"
#include "FakeDrivers.h"
#include "Light.h"
#include "Logger.h"
#include "Press.h"
#include "PressGeometry.h"

static Logger* logger;
static Light* charLed;
static FakeServo* servo;
static Press* press;

void setUp(void) {
  stubReset();
  logger = new Logger();
  charLed = new Light(CHARACTER_LED_PIN);
  servo = new FakeServo();
  press = new Press(logger, SERVO_PIN, charLed, servo);
}

void tearDown(void) {
  delete press;
  delete servo;
  delete charLed;
  delete logger;
}

// Brings the press up and then forgets the startup traffic, so each test reads
// only the calls its own press() made.
static void initializeAndClear(void) {
  press->initialize();
  servo->clear();
  servo->attached = true;
}

// --- startup -------------------------------------------------------------

void test_initialize_attaches_the_configured_pin_first(void) {
  press->initialize();
  TEST_ASSERT_TRUE(servo->calls.size() >= 2);
  TEST_ASSERT_EQUAL_INT(ServoCall::ATTACH, servo->calls[0].kind);
  TEST_ASSERT_EQUAL_INT(SERVO_PIN, servo->calls[0].value);
}

void test_initialize_parks_at_rest(void) {
  press->initialize();
  TEST_ASSERT_EQUAL_INT(ServoCall::WRITE, servo->calls[1].kind);
  TEST_ASSERT_EQUAL_INT(REST_ANGLE, servo->calls[1].value);
}

// --- travel --------------------------------------------------------------
// The geometry is already tested. What is tested here is that Press asks the
// servo for the angle the geometry returned, which is a separate claim.

void test_the_press_reaches_the_angle_the_geometry_asks_for(void) {
  initializeAndClear();
  press->press(false, 9, false);
  TEST_ASSERT_EQUAL_INT(
      pressPeakAngle(REST_ANGLE, STAMP_ANGLE, PRESS_BITE_AT_MAX_FORCE, 9),
      servo->minAngle());
}

void test_force_one_stops_at_the_taught_touch_point(void) {
  initializeAndClear();
  press->press(false, 1, false);
  TEST_ASSERT_EQUAL_INT(STAMP_ANGLE, servo->minAngle());
}

void test_a_bigger_force_actually_reaches_further_in(void) {
  // The question the bench could not settle: force 1 to 9 spans only
  // PRESS_BITE_AT_MAX_FORCE degrees, too little to see on a moving arm.
  int previous = REST_ANGLE + 1;
  for (int force = 1; force <= 9; force++) {
    tearDown();
    setUp();
    initializeAndClear();
    press->press(false, force, false);
    const int reached = servo->minAngle();
    TEST_ASSERT_TRUE_MESSAGE(reached < previous,
                             "each force must reach strictly further in");
    previous = reached;
  }
}

void test_the_press_always_comes_back_to_rest(void) {
  initializeAndClear();
  press->press(false, 7, false);
  const std::vector<int> written = servo->angles();
  TEST_ASSERT_EQUAL_INT(REST_ANGLE, written[written.size() - 1]);
}

void test_the_press_never_travels_past_rest_or_past_the_peak(void) {
  initializeAndClear();
  press->press(false, 9, false);
  const int peak =
      pressPeakAngle(REST_ANGLE, STAMP_ANGLE, PRESS_BITE_AT_MAX_FORCE, 9);
  TEST_ASSERT_EQUAL_INT(REST_ANGLE, servo->maxAngle());
  TEST_ASSERT_EQUAL_INT(peak, servo->minAngle());
}

void test_the_servo_is_walked_one_degree_at_a_time(void) {
  // A servo handed a big jump snaps to it at full speed, which is what slams
  // a printed press into the daisy wheel. Every step must be one degree.
  initializeAndClear();
  press->press(false, 9, false);
  const std::vector<int> written = servo->angles();
  for (size_t i = 1; i < written.size(); i++) {
    const int step = written[i] - written[i - 1];
    TEST_ASSERT_TRUE_MESSAGE(step >= -1 && step <= 1,
                             "servo must never be asked to jump");
  }
}

void test_a_missing_force_setting_still_presses(void) {
  // Settings::getForceFactor() returns 0 before the EEPROM key exists.
  initializeAndClear();
  press->press(false, 0, false);
  TEST_ASSERT_EQUAL_INT(STAMP_ANGLE, servo->minAngle());
}

// --- dwell ---------------------------------------------------------------
// How long the servo is held against the daisy wheel is the stall-current
// question in Press.h, and it was previously unmeasurable off a scope.

void test_a_printing_press_holds_at_peak_for_the_normal_dwell(void) {
  initializeAndClear();
  press->press(false, 5, false);
  const int peak =
      pressPeakAngle(REST_ANGLE, STAMP_ANGLE, PRESS_BITE_AT_MAX_FORCE, 5);
  TEST_ASSERT_EQUAL_UINT32(PRESS_DWELL_MS + 2 * PRESS_STEP_QUICK_MS,
                           servo->longestHoldAt(peak));
}

void test_the_calibration_press_holds_for_the_long_dwell(void) {
  // Two ramp steps longer than PRESS_TEST_DWELL_MS, because the peak is
  // written once on the way in and once on the way out with a step's wait
  // after each. At 2000ms plus 200 it hardly matters; at a force that really
  // bit, it would be 200ms of extra stall current, so the rule is pinned
  // here rather than left to be rediscovered.
  initializeAndClear();
  press->press(false, CALIBRATION_VALUE_MIN, true);
  TEST_ASSERT_EQUAL_UINT32(PRESS_TEST_DWELL_MS + 2 * PRESS_STEP_SLOW_MS,
                           servo->longestHoldAt(STAMP_ANGLE));
}

void test_a_strong_press_holds_two_ramp_steps_longer_than_the_dwell(void) {
  initializeAndClear();
  press->press(true, 9, false);
  const int peak =
      pressPeakAngle(REST_ANGLE, STAMP_ANGLE, PRESS_BITE_AT_MAX_FORCE, 9);
  TEST_ASSERT_EQUAL_UINT32(PRESS_DWELL_MS + 2 * PRESS_STEP_STRONG_MS,
                           servo->longestHoldAt(peak));
}

void test_the_long_dwell_is_eight_times_the_short_one(void) {
  // Press.h's warning depends on this ratio. If someone raises PRESS_DWELL_MS
  // without looking at PRESS_TEST_DWELL_MS, the comment stops being true.
  TEST_ASSERT_EQUAL_INT(8 * PRESS_DWELL_MS, PRESS_TEST_DWELL_MS);
}

// --- what force does and does not change ---------------------------------
// Press.h promises that strong and slow change only how quickly the press
// gets there, and that force alone changes how far in it goes.

void test_slow_and_strong_change_timing_not_travel(void) {
  const int expected =
      pressPeakAngle(REST_ANGLE, STAMP_ANGLE, PRESS_BITE_AT_MAX_FORCE, 5);

  initializeAndClear();
  press->press(false, 5, false);
  TEST_ASSERT_EQUAL_INT(expected, servo->minAngle());

  tearDown();
  setUp();
  initializeAndClear();
  press->press(true, 5, false);
  TEST_ASSERT_EQUAL_INT(expected, servo->minAngle());

  tearDown();
  setUp();
  initializeAndClear();
  press->press(false, 5, true);
  TEST_ASSERT_EQUAL_INT(expected, servo->minAngle());
}

void test_a_strong_press_eases_through_each_degree(void) {
  // Cutting presses three times and needs each one to land the same way, so
  // it pays for the slower ramp.
  initializeAndClear();
  press->press(false, 5, false);
  const unsigned long quick = millis();

  tearDown();
  setUp();
  initializeAndClear();
  press->press(true, 5, false);
  const unsigned long strong = millis();

  TEST_ASSERT_TRUE_MESSAGE(strong > quick,
                           "a strong press must take longer than a quick one");
}

// --- the character LED ---------------------------------------------------

void test_the_char_led_goes_bright_for_the_press_and_dim_after(void) {
  initializeAndClear();
  stubAnalogWrites().clear();
  press->press(false, 5, false);

  const std::vector<StubPinWrite>& writes = stubAnalogWrites();
  TEST_ASSERT_EQUAL_INT(2, (int)writes.size());
  TEST_ASSERT_EQUAL_INT(CHARACTER_LED_PIN, writes[0].pin);
  TEST_ASSERT_EQUAL_INT(128, writes[0].value);
  TEST_ASSERT_EQUAL_INT(25, writes[1].value);
  TEST_ASSERT_TRUE_MESSAGE(writes[0].atMs < writes[1].atMs,
                           "the LED must be lit before the press, not after");
}

// --- the bench paths -----------------------------------------------------

void test_hold_writes_a_raw_angle_the_geometry_would_refuse(void) {
  initializeAndClear();
  press->hold(120);
  TEST_ASSERT_EQUAL_INT(1, (int)servo->calls.size());
  TEST_ASSERT_EQUAL_INT(120, servo->calls[0].value);
}

void test_release_stops_driving_the_servo(void) {
  initializeAndClear();
  press->release();
  TEST_ASSERT_FALSE(servo->attached);
  TEST_ASSERT_EQUAL_INT(ServoCall::DETACH, servo->calls[0].kind);
}

void test_engage_drives_the_same_pin_again(void) {
  initializeAndClear();
  press->release();
  press->engage();
  TEST_ASSERT_TRUE(servo->attached);
  TEST_ASSERT_EQUAL_INT(ServoCall::ATTACH, servo->calls[1].kind);
  TEST_ASSERT_EQUAL_INT(SERVO_PIN, servo->calls[1].value);
}

void test_rest_is_a_single_write_and_does_not_ramp(void) {
  initializeAndClear();
  press->rest();
  TEST_ASSERT_EQUAL_INT(1, (int)servo->calls.size());
  TEST_ASSERT_EQUAL_INT(REST_ANGLE, servo->calls[0].value);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_initialize_attaches_the_configured_pin_first);
  RUN_TEST(test_initialize_parks_at_rest);
  RUN_TEST(test_the_press_reaches_the_angle_the_geometry_asks_for);
  RUN_TEST(test_force_one_stops_at_the_taught_touch_point);
  RUN_TEST(test_a_bigger_force_actually_reaches_further_in);
  RUN_TEST(test_the_press_always_comes_back_to_rest);
  RUN_TEST(test_the_press_never_travels_past_rest_or_past_the_peak);
  RUN_TEST(test_the_servo_is_walked_one_degree_at_a_time);
  RUN_TEST(test_a_missing_force_setting_still_presses);
  RUN_TEST(test_a_printing_press_holds_at_peak_for_the_normal_dwell);
  RUN_TEST(test_the_calibration_press_holds_for_the_long_dwell);
  RUN_TEST(test_a_strong_press_holds_two_ramp_steps_longer_than_the_dwell);
  RUN_TEST(test_the_long_dwell_is_eight_times_the_short_one);
  RUN_TEST(test_slow_and_strong_change_timing_not_travel);
  RUN_TEST(test_a_strong_press_eases_through_each_degree);
  RUN_TEST(test_the_char_led_goes_bright_for_the_press_and_dim_after);
  RUN_TEST(test_hold_writes_a_raw_angle_the_geometry_would_refuse);
  RUN_TEST(test_release_stops_driving_the_servo);
  RUN_TEST(test_engage_drives_the_same_pin_again);
  RUN_TEST(test_rest_is_a_single_write_and_does_not_ramp);
  return UNITY_END();
}
