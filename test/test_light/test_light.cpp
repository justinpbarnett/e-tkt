// Host-side tests for the LED patterns.
//
// The blink and the fade at the end of a print used to be loops inside
// ETKT::tagCommandInternal(), which meant the only way to check either one was
// to print a label and watch the board. They are behaviour, so they belong to
// the thing that owns the LED, and here they are asserted against the writes
// the stub records rather than against somebody's memory of the bench.
//
// Run with:  pio test -e native
#include <unity.h>

#include <vector>

#include "Arduino.h"
#include "Light.h"

static const uint8_t PIN = 5;
static Light* led;

void setUp(void) {
  stubReset();
  led = new Light(PIN);
}

void tearDown(void) { delete led; }

// Every value written to the LED's pin, in order.
static std::vector<int> written(void) {
  std::vector<int> values;
  for (size_t i = 0; i < stubAnalogWrites().size(); i++) {
    if (stubAnalogWrites()[i].pin == PIN) {
      values.push_back(stubAnalogWrites()[i].value);
    }
  }
  return values;
}

// --- the scale Light hides -----------------------------------------------
// ETKT used to write i / 128.0f by hand, rebuilding the very constant the
// wrapper exists to hide. Nothing outside Light may know the number now, so
// these pin what the fractions mean instead.

void test_full_brightness_is_the_top_of_the_range(void) {
  led->on(LIGHT_FULL);
  led->off();
  const std::vector<int> values = written();
  TEST_ASSERT_EQUAL_INT(2, (int)values.size());
  TEST_ASSERT_TRUE_MESSAGE(values[0] > 0, "full brightness must light the LED");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, values[1], "off must write dark");
}

void test_brightness_scales_between_off_and_full(void) {
  led->on(LIGHT_FULL);
  led->on(LIGHT_HALF);
  led->on(LIGHT_DIM);
  led->on(LIGHT_FAINT);
  const std::vector<int> values = written();
  TEST_ASSERT_TRUE(values[0] > values[1]);
  TEST_ASSERT_TRUE(values[1] > values[2]);
  TEST_ASSERT_TRUE(values[2] > values[3]);
  TEST_ASSERT_TRUE(values[3] > 0);
}

void test_brightness_above_full_is_clamped(void) {
  led->on(LIGHT_FULL);
  led->on(4.0f);
  const std::vector<int> values = written();
  TEST_ASSERT_EQUAL_INT_MESSAGE(values[0], values[1],
                                "anything above 1.0 must land on full, not on "
                                "a duty cycle past the end of the range");
}

void test_negative_brightness_is_dark(void) {
  led->on(-1.0f);
  TEST_ASSERT_EQUAL_INT(0, written()[0]);
}

// --- blink ---------------------------------------------------------------

void test_blink_writes_two_values_per_cycle(void) {
  led->blink(5, LIGHT_HALF, 100, 100);
  TEST_ASSERT_EQUAL_INT(10, (int)written().size());
}

void test_blink_starts_dark_and_ends_lit(void) {
  // Faithful to the sequence this replaced: it goes off first so the LED is
  // seen to change even when it was already on, and the fade that follows
  // takes over while it is still lit.
  led->blink(3, LIGHT_HALF, 100, 100);
  const std::vector<int> values = written();
  TEST_ASSERT_EQUAL_INT(0, values[0]);
  TEST_ASSERT_TRUE(values[values.size() - 1] > 0);
}

void test_blink_alternates_dark_and_lit(void) {
  led->blink(4, LIGHT_HALF, 100, 100);
  const std::vector<int> values = written();
  for (size_t i = 0; i < values.size(); i++) {
    if (i % 2 == 0) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, values[i], "even writes must be dark");
    } else {
      TEST_ASSERT_TRUE_MESSAGE(values[i] > 0, "odd writes must be lit");
    }
  }
}

void test_blink_takes_the_time_it_was_given(void) {
  led->blink(5, LIGHT_HALF, 100, 100);
  TEST_ASSERT_EQUAL_UINT32(5 * 200, millis());
}

void test_blink_honours_asymmetric_timings(void) {
  led->blink(2, LIGHT_HALF, 300, 50);
  TEST_ASSERT_EQUAL_UINT32(2 * 350, millis());
}

void test_blink_zero_times_does_nothing(void) {
  led->blink(0, LIGHT_HALF, 100, 100);
  TEST_ASSERT_EQUAL_INT(0, (int)written().size());
  TEST_ASSERT_EQUAL_UINT32(0, millis());
}

// --- fade ----------------------------------------------------------------

void test_fade_ends_dark(void) {
  led->fadeOut(LIGHT_FULL, 1000);
  const std::vector<int> values = written();
  TEST_ASSERT_EQUAL_INT(0, values[values.size() - 1]);
}

void test_fade_never_brightens(void) {
  led->fadeOut(LIGHT_FULL, 1000);
  const std::vector<int> values = written();
  for (size_t i = 1; i < values.size(); i++) {
    TEST_ASSERT_TRUE_MESSAGE(values[i] < values[i - 1],
                             "a fade must only ever get darker");
  }
}

void test_fade_starts_where_it_was_told_to(void) {
  led->on(LIGHT_HALF);
  const int half = written()[0];
  stubReset();
  led->fadeOut(LIGHT_HALF, 1000);
  TEST_ASSERT_EQUAL_INT(half, written()[0]);
}

void test_fade_takes_roughly_the_time_it_was_given(void) {
  // The step count is Light's business, so the duration cannot land exactly:
  // it is a whole number of equal steps. Within one step is the promise.
  led->fadeOut(LIGHT_FULL, 3225);
  const unsigned long elapsed = millis();
  const unsigned long step = 3225 / written().size();
  TEST_ASSERT_TRUE_MESSAGE(elapsed <= 3225 && elapsed + step > 3225,
                           "fade must fill the duration it was given");
}

void test_a_shorter_fade_is_shorter(void) {
  led->fadeOut(LIGHT_FULL, 3200);
  const unsigned long slow = millis();
  stubReset();
  led->fadeOut(LIGHT_FULL, 400);
  TEST_ASSERT_TRUE(millis() < slow);
}

void test_fade_from_dark_just_goes_dark(void) {
  led->fadeOut(0.0f, 1000);
  const std::vector<int> values = written();
  TEST_ASSERT_EQUAL_INT(1, (int)values.size());
  TEST_ASSERT_EQUAL_INT(0, values[0]);
}

void test_fade_uses_every_level_it_has(void) {
  // A fade with only a handful of steps is a flicker. Whatever the internal
  // scale is, a full fade has to walk it.
  led->fadeOut(LIGHT_FULL, 3225);
  TEST_ASSERT_TRUE_MESSAGE(written().size() > 32,
                           "a full fade must be smooth, not stepped");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_full_brightness_is_the_top_of_the_range);
  RUN_TEST(test_brightness_scales_between_off_and_full);
  RUN_TEST(test_brightness_above_full_is_clamped);
  RUN_TEST(test_negative_brightness_is_dark);
  RUN_TEST(test_blink_writes_two_values_per_cycle);
  RUN_TEST(test_blink_starts_dark_and_ends_lit);
  RUN_TEST(test_blink_alternates_dark_and_lit);
  RUN_TEST(test_blink_takes_the_time_it_was_given);
  RUN_TEST(test_blink_honours_asymmetric_timings);
  RUN_TEST(test_blink_zero_times_does_nothing);
  RUN_TEST(test_fade_ends_dark);
  RUN_TEST(test_fade_never_brightens);
  RUN_TEST(test_fade_starts_where_it_was_told_to);
  RUN_TEST(test_fade_takes_roughly_the_time_it_was_given);
  RUN_TEST(test_a_shorter_fade_is_shorter);
  RUN_TEST(test_fade_from_dark_just_goes_dark);
  RUN_TEST(test_fade_uses_every_level_it_has);
  return UNITY_END();
}
