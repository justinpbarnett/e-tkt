// Host-side tests for what a label is allowed to say.
//
// The set of printable characters used to be written out in five places --
// the wheel map here, a comment in ETKT.cpp, a comment and a regex in
// script.js, a second copy of both, and a hint line in index.html -- and no
// two of them agreed. The device now serves one answer and these tests pin
// what that answer is made of.
//
// Run with:  pio test -e native
#include <unity.h>

#include <set>

#include "CharacterSet.h"
#include "Utility.h"

void setUp(void) {}
void tearDown(void) {}

// --- the printable set ---------------------------------------------------

void test_a_space_is_printable(void) {
  // A space has no wheel slot: it is the feeder advancing with nothing
  // pressed into the tape. It still has to be typeable.
  TEST_ASSERT_TRUE(printableCharacters().indexOf(String(" ")) >= 0);
}

void test_the_cut_mark_is_not_printable(void) {
  // The wheel drives to * to cut. A label may not ask for one.
  TEST_ASSERT_EQUAL_INT(-1,
                        printableCharacters().indexOf(String(CUT_CHARACTER)));
}

void test_the_cut_mark_is_still_on_the_wheel(void) {
  // ETKT::cut() moves to it by name, so dropping it from CHARACTERS would
  // stop the machine cutting rather than just stop it being typeable.
  TEST_ASSERT_TRUE(CHARACTERS.find(CUT_CHARACTER) != CHARACTERS.end());
}

void test_every_wheel_character_but_the_cut_mark_is_printable(void) {
  const String printable = printableCharacters();
  for (std::map<String, int>::const_iterator it = CHARACTERS.begin();
       it != CHARACTERS.end(); ++it) {
    if (it->first == CUT_CHARACTER) {
      continue;
    }
    TEST_ASSERT_TRUE_MESSAGE(printable.indexOf(it->first) >= 0,
                             it->first.c_str());
  }
}

void test_the_printable_set_holds_nothing_the_wheel_lacks(void) {
  // Paired with the test above: every wheel character is in there, and the
  // count matches, so there is nothing extra either.
  const String printable = printableCharacters();
  const int expected = 1 + (int)CHARACTERS.size() - 1;  // the space, less *
  TEST_ASSERT_EQUAL_INT(expected, Utility::utf8Length(printable));
}

void test_no_character_is_printable_twice(void) {
  const String printable = printableCharacters();
  const int length = Utility::utf8Length(printable);
  std::set<std::string> seen;
  for (int i = 0; i < length; i++) {
    const String character = Utility::utf8CharAt(printable, i);
    TEST_ASSERT_TRUE_MESSAGE(seen.insert(std::string(character.c_str())).second,
                             character.c_str());
  }
}

// --- the characters the wheel does not carry -----------------------------

void test_the_wheel_has_no_zero_of_its_own(void) {
  TEST_ASSERT_EQUAL_INT(CHARACTERS.at("O"), CHARACTERS.at("0"));
}

void test_the_wheel_has_no_one_of_its_own(void) {
  TEST_ASSERT_EQUAL_INT(CHARACTERS.at("I"), CHARACTERS.at("1"));
}

void test_every_alias_is_something_you_can_type(void) {
  const String printable = printableCharacters();
  for (std::map<String, String>::const_iterator it = CHARACTER_ALIASES.begin();
       it != CHARACTER_ALIASES.end(); ++it) {
    TEST_ASSERT_TRUE_MESSAGE(printable.indexOf(it->first) >= 0,
                             it->first.c_str());
  }
}

void test_every_alias_prints_something_on_the_wheel(void) {
  for (std::map<String, String>::const_iterator it = CHARACTER_ALIASES.begin();
       it != CHARACTER_ALIASES.end(); ++it) {
    TEST_ASSERT_TRUE_MESSAGE(CHARACTERS.find(it->second) != CHARACTERS.end(),
                             it->second.c_str());
  }
}

void test_an_alias_shares_the_slot_it_prints_from(void) {
  // This is the whole claim: typing a 0 drives to the same place as typing
  // an O, which is why the panel has to say so before the tape is spent.
  for (std::map<String, String>::const_iterator it = CHARACTER_ALIASES.begin();
       it != CHARACTER_ALIASES.end(); ++it) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(CHARACTERS.at(it->second),
                                  CHARACTERS.at(it->first), it->first.c_str());
  }
}

void test_a_character_with_a_slot_of_its_own_is_not_an_alias(void) {
  TEST_ASSERT_TRUE(CHARACTER_ALIASES.find("A") == CHARACTER_ALIASES.end());
  TEST_ASSERT_TRUE(CHARACTER_ALIASES.find("2") == CHARACTER_ALIASES.end());
}

// --- walking a label -----------------------------------------------------
// ETKT::tagCommandInternal steps a label with these two. They read the
// leading byte of each character, and that byte has its high bit set for
// every symbol on the wheel, so they only work if it is not read as a sign.

void test_a_symbol_counts_as_one_character(void) {
  TEST_ASSERT_EQUAL_INT(1, Utility::utf8Length(String("♡")));
  TEST_ASSERT_EQUAL_INT(1, Utility::utf8Length(String("☆")));
  TEST_ASSERT_EQUAL_INT(1, Utility::utf8Length(String("♪")));
  TEST_ASSERT_EQUAL_INT(1, Utility::utf8Length(String("€")));
}

void test_a_label_of_symbols_and_letters_counts_right(void) {
  TEST_ASSERT_EQUAL_INT(5, Utility::utf8Length(String("A♡B☆C")));
}

void test_a_symbol_comes_back_whole(void) {
  TEST_ASSERT_TRUE(Utility::utf8CharAt(String("A♡B"), 1) == String("♡"));
  TEST_ASSERT_TRUE(Utility::utf8CharAt(String("A♡B"), 2) == String("B"));
}

void test_every_wheel_symbol_survives_a_round_trip(void) {
  for (std::map<String, int>::const_iterator it = CHARACTERS.begin();
       it != CHARACTERS.end(); ++it) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, Utility::utf8Length(it->first),
                                  it->first.c_str());
    TEST_ASSERT_TRUE_MESSAGE(Utility::utf8CharAt(it->first, 0) == it->first,
                             it->first.c_str());
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_space_is_printable);
  RUN_TEST(test_the_cut_mark_is_not_printable);
  RUN_TEST(test_the_cut_mark_is_still_on_the_wheel);
  RUN_TEST(test_every_wheel_character_but_the_cut_mark_is_printable);
  RUN_TEST(test_the_printable_set_holds_nothing_the_wheel_lacks);
  RUN_TEST(test_no_character_is_printable_twice);
  RUN_TEST(test_the_wheel_has_no_zero_of_its_own);
  RUN_TEST(test_the_wheel_has_no_one_of_its_own);
  RUN_TEST(test_every_alias_is_something_you_can_type);
  RUN_TEST(test_every_alias_prints_something_on_the_wheel);
  RUN_TEST(test_an_alias_shares_the_slot_it_prints_from);
  RUN_TEST(test_a_character_with_a_slot_of_its_own_is_not_an_alias);
  RUN_TEST(test_a_symbol_counts_as_one_character);
  RUN_TEST(test_a_label_of_symbols_and_letters_counts_right);
  RUN_TEST(test_a_symbol_comes_back_whole);
  RUN_TEST(test_every_wheel_symbol_survives_a_round_trip);
  return UNITY_END();
}
