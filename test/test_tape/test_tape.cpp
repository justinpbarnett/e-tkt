// Host-side tests for the tape arithmetic in Tape.h.
//
// The machine cannot see the tape, so "how much is left" and "how many more
// labels fit" are sums over a count of feeds. They have to agree with what
// the tag handler actually feeds, or printing to the end of the roll stops a
// label early or runs one off the end. Run with:  pio test -e native
#include <unity.h>

#include <fstream>
#include <iterator>
#include <string>

#include "ArduinoJson.h"
#include "Configuration.h"
#include "Tape.h"

void setUp(void) {}
void tearDown(void) {}

// --- the cases the panel is held to as well ---------------------------------
//
// data/tape.js restates these sums for the panel, and test/panel/tape.test.js
// holds it to the same cases as this file, from test/vectors/tape.json. A
// case added there is checked in both languages.

// The cases, parsed. pio test runs the program from the project directory.
// What it returns is good until the next call.
static JsonObject vectors(void) {
  static DynamicJsonDocument doc(16384);
  std::ifstream file("test/vectors/tape.json");
  TEST_ASSERT_TRUE_MESSAGE(file.is_open(),
                           "cannot open test/vectors/tape.json");
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  const DeserializationError error = deserializeJson(doc, text);
  TEST_ASSERT_EQUAL_STRING("Ok", error.c_str());
  return doc.as<JsonObject>();
}

// The cases under one key. Never empty: a list that is missing would
// otherwise pass, having no case in it to fail.
static JsonArray cases(const char* key) {
  const JsonArray list = vectors()[key];
  TEST_ASSERT_TRUE_MESSAGE(list.size() > 0, key);
  return list;
}

// One of a case's numbers. A key that is missing or misspelt fails, rather
// than reading as the zero a case may well expect.
static int number(JsonObject item, const char* key) {
  TEST_ASSERT_TRUE_MESSAGE(item[key].is<int>(), key);
  return item[key].as<int>();
}

void test_the_cases_are_worked_for_this_machine(void) {
  // As /api/capabilities serves it to the panel. A change to one of these
  // has to be worked through the cases by hand, not just made here.
  const JsonObject device = vectors()["device"];
  TEST_ASSERT_EQUAL_INT(number(device["label"], "minimum"),
                        MIN_LABEL_CHARACTERS);
  TEST_ASSERT_EQUAL_INT(number(device["label"], "maximum"),
                        MAX_LABEL_CHARACTERS);
  TEST_ASSERT_EQUAL_INT(number(device["feed"], "lead"), LEAD_FEEDS);
  TEST_ASSERT_EQUAL_INT(number(device["feed"], "length_um"), FEED_LENGTH_UM);
}

void test_each_label_takes_the_feeds_the_cases_say(void) {
  for (JsonObject label : cases("labels")) {
    const char* why = label["case"];
    const int characters = number(label, "characters");
    TEST_ASSERT_EQUAL_INT_MESSAGE(number(label, "top_up"),
                                  topUpFeeds(characters), why);
    TEST_ASSERT_EQUAL_INT_MESSAGE(number(label, "feeds"),
                                  labelFeeds(characters), why);
    TEST_ASSERT_EQUAL_INT64_MESSAGE(number(label, "mm"),
                                    tapeUsedMm(labelFeeds(characters)), why);
  }
}

void test_the_tape_left_fits_the_labels_the_cases_say(void) {
  for (JsonObject fit : cases("fit")) {
    const char* why = fit["case"];
    TEST_ASSERT_EQUAL_INT64_MESSAGE(
        number(fit, "labels"),
        labelsThatFit(number(fit, "left_mm"), number(fit, "characters")), why);
  }
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
  RUN_TEST(test_the_cases_are_worked_for_this_machine);
  RUN_TEST(test_each_label_takes_the_feeds_the_cases_say);
  RUN_TEST(test_the_tape_left_fits_the_labels_the_cases_say);
  RUN_TEST(test_each_feed_uses_the_configured_length);
  RUN_TEST(test_negative_feeds_use_nothing);
  RUN_TEST(test_a_long_count_does_not_overflow);
  RUN_TEST(test_what_is_left_is_the_roll_less_what_was_fed);
  RUN_TEST(test_an_overrun_roll_reads_empty_not_negative);
  RUN_TEST(test_the_copy_limit_never_cuts_a_roll_short);
  RUN_TEST(test_roll_lengths_are_bounded);
  RUN_TEST(test_the_default_roll_is_one_a_request_may_declare);
  RUN_TEST(test_copies_are_bounded);
  return UNITY_END();
}
