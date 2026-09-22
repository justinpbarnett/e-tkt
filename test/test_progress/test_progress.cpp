// Host-side tests for the one definition of print progress.
//
// Before this existed the percentage was derived in three places: ETKT while
// printing, Display for the "XX%" caption, and script.js for the button. Two
// of the three subtracted the same "not finished yet" one, so the browser
// always read a point lower than the OLED beside it.
// Run with:  pio test -e native
#include <unity.h>

#include "Progress.h"

void setUp(void) {}
void tearDown(void) {}

// --- the ordinary case ---------------------------------------------------

void test_percent_is_characters_done_over_length(void) {
  TEST_ASSERT_EQUAL_INT(20, progressPercent(1, 5));
  TEST_ASSERT_EQUAL_INT(40, progressPercent(2, 5));
  TEST_ASSERT_EQUAL_INT(60, progressPercent(3, 5));
  TEST_ASSERT_EQUAL_INT(80, progressPercent(4, 5));
}

void test_nothing_done_is_zero(void) {
  TEST_ASSERT_EQUAL_INT(0, progressPercent(0, 5));
}

// --- the cap -------------------------------------------------------------
// The last character is pressed, but the feed padding and the cut still have
// to happen. Reporting 100 there tells the user it is safe to grab the tape.

void test_the_last_character_reports_ninety_nine_not_one_hundred(void) {
  TEST_ASSERT_EQUAL_INT(PROGRESS_MAX_WHILE_PRINTING, progressPercent(5, 5));
  TEST_ASSERT_EQUAL_INT(99, progressPercent(5, 5));
}

void test_a_single_character_label_also_caps(void) {
  TEST_ASSERT_EQUAL_INT(99, progressPercent(1, 1));
}

void test_the_cap_is_applied_once_not_twice(void) {
  // The browser used to subtract its own 1 from whatever arrived, so a label
  // finishing at 99 displayed 98. One definition, one subtraction.
  TEST_ASSERT_EQUAL_INT(99, progressPercent(7, 7));
}

// --- guards --------------------------------------------------------------

void test_zero_length_does_not_divide_by_zero(void) {
  TEST_ASSERT_EQUAL_INT(0, progressPercent(0, 0));
  TEST_ASSERT_EQUAL_INT(0, progressPercent(3, 0));
}

void test_negative_input_reports_zero(void) {
  TEST_ASSERT_EQUAL_INT(0, progressPercent(-1, 5));
  TEST_ASSERT_EQUAL_INT(0, progressPercent(1, -5));
}

void test_more_done_than_length_still_caps(void) {
  TEST_ASSERT_EQUAL_INT(99, progressPercent(9, 5));
}

// --- shape ---------------------------------------------------------------

void test_progress_never_moves_backwards(void) {
  for (int length = 1; length <= 40; length++) {
    int previous = 0;
    for (int done = 0; done <= length; done++) {
      const int current = progressPercent(done, length);
      TEST_ASSERT_TRUE_MESSAGE(current >= previous, "progress went backwards");
      TEST_ASSERT_TRUE(current >= 0 && current <= 99);
      previous = current;
    }
  }
}

// --- scroll offset --------------------------------------------------------
//
// The label is drawn as one strip and the screen is a window onto it. These
// describe where that window sits. All widths are pixels; 128 is the OLED.

void test_a_label_that_fits_does_not_scroll(void) {
  TEST_ASSERT_EQUAL_INT(0, scrollOffset(0, 100, 128));
  TEST_ASSERT_EQUAL_INT(0, scrollOffset(50, 100, 128));
  TEST_ASSERT_EQUAL_INT(0, scrollOffset(100, 100, 128));
}

void test_an_exactly_full_label_does_not_scroll(void) {
  TEST_ASSERT_EQUAL_INT(0, scrollOffset(64, 128, 128));
}

void test_a_long_label_holds_still_until_the_press_passes_the_middle(void) {
  // Nothing moves while the character being pressed is still in the left
  // half of the glass.
  TEST_ASSERT_EQUAL_INT(0, scrollOffset(0, 400, 128));
  TEST_ASSERT_EQUAL_INT(0, scrollOffset(63, 400, 128));
  TEST_ASSERT_EQUAL_INT(0, scrollOffset(64, 400, 128));
}

void test_a_long_label_centres_the_character_being_pressed(void) {
  // Past the middle, the strip slides so the press stays at x = 64.
  TEST_ASSERT_EQUAL_INT(36, scrollOffset(100, 400, 128));
  TEST_ASSERT_EQUAL_INT(136, scrollOffset(200, 400, 128));
}

void test_the_end_of_a_long_label_stops_at_the_right_edge(void) {
  // Centring the last characters would drag blank space onto the screen.
  // The strip stops with its right edge on the right edge of the glass.
  TEST_ASSERT_EQUAL_INT(272, scrollOffset(390, 400, 128));
  TEST_ASSERT_EQUAL_INT(272, scrollOffset(400, 400, 128));
}

void test_the_offset_never_goes_backwards_past_the_start(void) {
  // A label barely over the width would otherwise compute a negative
  // offset at the end and push the strip off the left of the glass.
  TEST_ASSERT_EQUAL_INT(1, scrollOffset(129, 129, 128));
  TEST_ASSERT_EQUAL_INT(0, scrollOffset(0, 129, 128));
}

void test_an_empty_label_has_no_offset(void) {
  TEST_ASSERT_EQUAL_INT(0, scrollOffset(0, 0, 128));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_percent_is_characters_done_over_length);
  RUN_TEST(test_nothing_done_is_zero);
  RUN_TEST(test_the_last_character_reports_ninety_nine_not_one_hundred);
  RUN_TEST(test_a_single_character_label_also_caps);
  RUN_TEST(test_the_cap_is_applied_once_not_twice);
  RUN_TEST(test_zero_length_does_not_divide_by_zero);
  RUN_TEST(test_negative_input_reports_zero);
  RUN_TEST(test_more_done_than_length_still_caps);
  RUN_TEST(test_progress_never_moves_backwards);
  RUN_TEST(test_a_label_that_fits_does_not_scroll);
  RUN_TEST(test_an_exactly_full_label_does_not_scroll);
  RUN_TEST(test_a_long_label_holds_still_until_the_press_passes_the_middle);
  RUN_TEST(test_a_long_label_centres_the_character_being_pressed);
  RUN_TEST(test_the_end_of_a_long_label_stops_at_the_right_edge);
  RUN_TEST(test_the_offset_never_goes_backwards_past_the_start);
  RUN_TEST(test_an_empty_label_has_no_offset);
  return UNITY_END();
}
