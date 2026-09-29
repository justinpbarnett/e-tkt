// Host-side tests for the tape arithmetic in Tape.h.
//
// The machine cannot see the tape, so "how much is left" and "how many more
// labels fit" are sums over a count of feeds. They have to agree with what
// the tag handler actually feeds, or printing to the end of the roll stops a
// label early or runs one off the end. Run with:  pio test -e native
#include <unity.h>

#include "Configuration.h"
#include "Tape.h"

void setUp(void) {}
void tearDown(void) {}

// --- one label -------------------------------------------------------------

void test_a_label_is_a_lead_then_a_feed_per_character(void) {
  // The panel pads every label to one past the minimum, so this is the
  // shortest thing it sends: one lead feed and seven characters.
  TEST_ASSERT_EQUAL_INT(8, labelFeeds(7));
  TEST_ASSERT_EQUAL_INT(LEAD_FEEDS + 30, labelFeeds(30));
  TEST_ASSERT_EQUAL_INT(LEAD_FEEDS + MAX_LABEL_CHARACTERS,
                        labelFeeds(MAX_LABEL_CHARACTERS));
}

void test_a_short_label_is_topped_up_to_the_minimum(void) {
  TEST_ASSERT_EQUAL_INT(MIN_LABEL_CHARACTERS - 3, topUpFeeds(3));
  TEST_ASSERT_EQUAL_INT(LEAD_FEEDS + MIN_LABEL_CHARACTERS, labelFeeds(3));
  TEST_ASSERT_EQUAL_INT(1, topUpFeeds(MIN_LABEL_CHARACTERS - 1));
}

void test_a_label_at_the_minimum_needs_no_top_up(void) {
  TEST_ASSERT_EQUAL_INT(0, topUpFeeds(MIN_LABEL_CHARACTERS));
  TEST_ASSERT_EQUAL_INT(0, topUpFeeds(MIN_LABEL_CHARACTERS + 1));
}

void test_a_single_letter_is_left_short(void) {
  // The one exception the tag handler has always made.
  TEST_ASSERT_EQUAL_INT(0, topUpFeeds(1));
  TEST_ASSERT_EQUAL_INT(LEAD_FEEDS + 1, labelFeeds(1));
}

void test_an_empty_label_still_feeds_a_whole_minimum(void) {
  TEST_ASSERT_EQUAL_INT(LEAD_FEEDS + MIN_LABEL_CHARACTERS, labelFeeds(0));
  TEST_ASSERT_EQUAL_INT(labelFeeds(0), labelFeeds(-4));
}

// --- the roll ----------------------------------------------------------------

void test_each_feed_uses_the_configured_length(void) {
  TEST_ASSERT_EQUAL_INT64(0, tapeUsedMm(0));
  TEST_ASSERT_EQUAL_INT64(FEED_LENGTH_UM / 1000, tapeUsedMm(1));
  TEST_ASSERT_EQUAL_INT64(3000, tapeUsedMm(3000000 / FEED_LENGTH_UM));
}

void test_negative_feeds_use_nothing(void) {
  TEST_ASSERT_EQUAL_INT64(0, tapeUsedMm(-5));
}

void test_a_long_count_does_not_overflow(void) {
  // 32 bits of feeds times micrometres would wrap here and report tape
  // coming back onto the roll.
  TEST_ASSERT_EQUAL_INT64(2000000LL * FEED_LENGTH_UM / 1000,
                          tapeUsedMm(2000000LL));
}

void test_what_is_left_is_the_roll_less_what_was_fed(void) {
  TEST_ASSERT_EQUAL_INT64(3000, remainingMm(3000, 0));
  TEST_ASSERT_EQUAL_INT64(3000 - 8 * FEED_LENGTH_UM / 1000,
                          remainingMm(3000, 8));
}

void test_an_overrun_roll_reads_empty_not_negative(void) {
  const long long feedsInARoll = 3000LL * 1000 / FEED_LENGTH_UM;
  TEST_ASSERT_EQUAL_INT64(0, remainingMm(3000, feedsInARoll));
  TEST_ASSERT_EQUAL_INT64(0, remainingMm(3000, feedsInARoll + 40));
}

// --- printing to the end of the roll ----------------------------------------

void test_labels_that_fit_rounds_down(void) {
  // 3 m at 32 mm a label is 93.75 labels. The 94th runs off the end.
  TEST_ASSERT_EQUAL_INT64(
      DEFAULT_ROLL_LENGTH_MM * 1000LL / (labelFeeds(7) * FEED_LENGTH_UM),
      labelsThatFit(DEFAULT_ROLL_LENGTH_MM, 7));
  TEST_ASSERT_EQUAL_INT64(93, labelsThatFit(3000, 7));
}

void test_exactly_one_label_left_fits_one(void) {
  const long long oneLabelMm = labelFeeds(7) * FEED_LENGTH_UM / 1000;
  TEST_ASSERT_EQUAL_INT64(1, labelsThatFit(oneLabelMm, 7));
  TEST_ASSERT_EQUAL_INT64(0, labelsThatFit(oneLabelMm - 1, 7));
}

void test_an_empty_roll_fits_nothing(void) {
  TEST_ASSERT_EQUAL_INT64(0, labelsThatFit(0, 7));
  TEST_ASSERT_EQUAL_INT64(0, labelsThatFit(-10, 7));
}

void test_the_copy_limit_never_cuts_a_roll_short(void) {
  // The cap exists to stop a request running for days, not to shorten a
  // print-to-the-end. The longest roll of the panel's shortest label has to
  // come in under it.
  TEST_ASSERT_TRUE(labelsThatFit(ROLL_LENGTH_MAX_MM,
                                 MIN_LABEL_CHARACTERS + 1) <= MAX_COPIES);
}

// --- what a request may say -------------------------------------------------

void test_roll_lengths_are_bounded(void) {
  TEST_ASSERT_FALSE(isValidRollLength(ROLL_LENGTH_MIN_MM - 1));
  TEST_ASSERT_TRUE(isValidRollLength(ROLL_LENGTH_MIN_MM));
  TEST_ASSERT_TRUE(isValidRollLength(ROLL_LENGTH_MAX_MM));
  TEST_ASSERT_FALSE(isValidRollLength(ROLL_LENGTH_MAX_MM + 1));
  TEST_ASSERT_FALSE(isValidRollLength(0));
  TEST_ASSERT_FALSE(isValidRollLength(-3000));
}

void test_the_default_roll_is_one_a_request_may_declare(void) {
  TEST_ASSERT_TRUE(isValidRollLength(DEFAULT_ROLL_LENGTH_MM));
}

void test_copies_are_bounded(void) {
  TEST_ASSERT_FALSE(isValidCopies(0));
  TEST_ASSERT_TRUE(isValidCopies(1));
  TEST_ASSERT_TRUE(isValidCopies(MAX_COPIES));
  TEST_ASSERT_FALSE(isValidCopies(MAX_COPIES + 1));
  TEST_ASSERT_FALSE(isValidCopies(-1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_label_is_a_lead_then_a_feed_per_character);
  RUN_TEST(test_a_short_label_is_topped_up_to_the_minimum);
  RUN_TEST(test_a_label_at_the_minimum_needs_no_top_up);
  RUN_TEST(test_a_single_letter_is_left_short);
  RUN_TEST(test_an_empty_label_still_feeds_a_whole_minimum);
  RUN_TEST(test_each_feed_uses_the_configured_length);
  RUN_TEST(test_negative_feeds_use_nothing);
  RUN_TEST(test_a_long_count_does_not_overflow);
  RUN_TEST(test_what_is_left_is_the_roll_less_what_was_fed);
  RUN_TEST(test_an_overrun_roll_reads_empty_not_negative);
  RUN_TEST(test_labels_that_fit_rounds_down);
  RUN_TEST(test_exactly_one_label_left_fits_one);
  RUN_TEST(test_an_empty_roll_fits_nothing);
  RUN_TEST(test_the_copy_limit_never_cuts_a_roll_short);
  RUN_TEST(test_roll_lengths_are_bounded);
  RUN_TEST(test_the_default_roll_is_one_a_request_may_declare);
  RUN_TEST(test_copies_are_bounded);
  return UNITY_END();
}
