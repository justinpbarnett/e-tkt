// Host-side tests for how long a melody note lasts.
//
// The arithmetic used to sit inline in Sound::playMelody as
// `2000 / atoi(&charDuration)`, where charDuration was one char on the stack
// with no NUL after it. atoi read past it, and a parse that came back 0 made
// that an integer division by zero -- which on the Xtensa is an exception and
// a reboot, not a NaN.
// Run with:  pio test -e native -f test_melody
#include <unity.h>

#include "Melody.h"

void setUp(void) {}
void tearDown(void) {}

// --- the ordinary case ---------------------------------------------------
// A digit says what fraction of the whole note this one lasts.

void test_the_digit_divides_the_whole_note(void) {
  TEST_ASSERT_EQUAL_INT(2000, melodyNoteMs("1", 0));
  TEST_ASSERT_EQUAL_INT(1000, melodyNoteMs("2", 0));
  TEST_ASSERT_EQUAL_INT(666, melodyNoteMs("3", 0));
  TEST_ASSERT_EQUAL_INT(500, melodyNoteMs("4", 0));
  TEST_ASSERT_EQUAL_INT(250, melodyNoteMs("8", 0));
}

void test_each_note_reads_its_own_digit(void) {
  TEST_ASSERT_EQUAL_INT(500, melodyNoteMs("48", 0));
  TEST_ASSERT_EQUAL_INT(250, melodyNoteMs("48", 1));
}

// --- what used to read past the end --------------------------------------
// The old code handed atoi a pointer to one stack byte. Whatever followed it
// in the frame got parsed too: an '8' with a stray '7' behind it came back 87,
// so a 250 ms note played for 22 ms.

void test_a_digit_does_not_absorb_what_follows_it(void) {
  TEST_ASSERT_EQUAL_INT(250, melodyNoteMs("87", 0));
  TEST_ASSERT_EQUAL_INT(2000, melodyNoteMs("19", 0));
}

// --- what used to divide by zero -----------------------------------------

void test_a_zero_is_the_whole_note_not_a_division_by_zero(void) {
  TEST_ASSERT_EQUAL_INT(MELODY_WHOLE_NOTE_MS, melodyNoteMs("0", 0));
}

void test_a_non_digit_is_the_whole_note(void) {
  TEST_ASSERT_EQUAL_INT(MELODY_WHOLE_NOTE_MS, melodyNoteMs(" ", 0));
  TEST_ASSERT_EQUAL_INT(MELODY_WHOLE_NOTE_MS, melodyNoteMs("x", 0));
  TEST_ASSERT_EQUAL_INT(MELODY_WHOLE_NOTE_MS, melodyNoteMs("-", 0));
}

// --- running off the string ----------------------------------------------
// The two strings are meant to be the same length. When they are not, the
// note still has to last something, and the whole note is loud enough to
// announce the typo.

void test_past_the_end_is_the_whole_note(void) {
  TEST_ASSERT_EQUAL_INT(MELODY_WHOLE_NOTE_MS, melodyNoteMs("48", 2));
  TEST_ASSERT_EQUAL_INT(MELODY_WHOLE_NOTE_MS, melodyNoteMs("", 0));
}

void test_before_the_start_is_the_whole_note(void) {
  TEST_ASSERT_EQUAL_INT(MELODY_WHOLE_NOTE_MS, melodyNoteMs("48", -1));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_the_digit_divides_the_whole_note);
  RUN_TEST(test_each_note_reads_its_own_digit);
  RUN_TEST(test_a_digit_does_not_absorb_what_follows_it);
  RUN_TEST(test_a_zero_is_the_whole_note_not_a_division_by_zero);
  RUN_TEST(test_a_non_digit_is_the_whole_note);
  RUN_TEST(test_past_the_end_is_the_whole_note);
  RUN_TEST(test_before_the_start_is_the_whole_note);
  return UNITY_END();
}
