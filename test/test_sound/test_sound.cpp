// Host-side tests for the tune a job opens with, and how long it takes.
//
// The buzzer is ESP32Tone's, stubbed in test/stubs/ESP32Tone.h: every note
// is recorded, and holds up the caller for its length on the virtual clock,
// as the real one does.
// Run with:  pio test -e native -f test_sound
#include <unity.h>

#include <vector>

#include "Arduino.h"
#include "CharacterSet.h"
#include "Sound.h"
#include "StopSignal.h"

static StopSignal* stopSignal;
static Sound* sound;

void setUp(void) {
  stubReset();
  stopSignal = new StopSignal();
  sound = new Sound(stopSignal);
  sound->initialize();
}

void tearDown(void) {
  delete sound;
  delete stopSignal;
}

static const char* const CALCULATOR_LABELS[] = {
    " TASCHENRECHNER ", " POCKET CALCULATOR ", " DENTAKU ", " CALCULADORA ",
    " MINI CALCULATEUR "};

// --- the tune ----------------------------------------------------------------

// Each character sounds its note for a tenth of a second, with half that of
// silence after it. A space has no note, and is only the silence.
void test_a_label_plays_the_note_of_each_character_it_has_one_for(void) {
  sound->playTune(" HELLO ");

  const char* notes[] = {"H", "E", "L", "L", "O"};
  TEST_ASSERT_EQUAL(5, stubTones().size());
  for (size_t i = 0; i < 5; i++) {
    const StubTone& tone = stubTones()[i];
    TEST_ASSERT_EQUAL_MESSAGE(characterNote(notes[i]), tone.frequency,
                              notes[i]);
    TEST_ASSERT_EQUAL_MESSAGE(100, tone.durationMs, notes[i]);
    // After the space, 150 ms a character.
    TEST_ASSERT_EQUAL_MESSAGE(50 + 150 * i, tone.atMs, notes[i]);
  }
  TEST_ASSERT_EQUAL(850, millis());
}

// On a label of more than 16 characters, each note from the sixth on is 2 ms
// shorter than the one before, down to 20 ms.
void test_the_notes_of_a_long_label_get_shorter_from_the_sixth(void) {
  sound->playTune(
      "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGH");

  TEST_ASSERT_EQUAL(60, stubTones().size());
  TEST_ASSERT_EQUAL(100, stubTones()[0].durationMs);
  TEST_ASSERT_EQUAL(100, stubTones()[4].durationMs);
  TEST_ASSERT_EQUAL(98, stubTones()[5].durationMs);
  TEST_ASSERT_EQUAL(76, stubTones()[16].durationMs);
  TEST_ASSERT_EQUAL(22, stubTones()[43].durationMs);
  TEST_ASSERT_EQUAL(20, stubTones()[44].durationMs);
  TEST_ASSERT_EQUAL(20, stubTones()[59].durationMs);
}

void test_a_label_of_16_characters_plays_every_note_whole(void) {
  sound->playTune("ABCDEFGHIJKLMNOP");

  TEST_ASSERT_EQUAL(16, stubTones().size());
  for (const StubTone& tone : stubTones()) {
    TEST_ASSERT_EQUAL(100, tone.durationMs);
  }
}

// Its 41 notes open on two eighths, a quarter and a third.
void test_the_pocket_calculator_labels_play_its_melody(void) {
  for (const char* label : CALCULATOR_LABELS) {
    stubTones().clear();
    sound->playTune(label);

    TEST_ASSERT_EQUAL_MESSAGE(41, stubTones().size(), label);
    TEST_ASSERT_EQUAL_MESSAGE(characterNote("4"), stubTones()[0].frequency,
                              label);
    TEST_ASSERT_EQUAL_MESSAGE(250, stubTones()[0].durationMs, label);
    TEST_ASSERT_EQUAL_MESSAGE(250, stubTones()[1].durationMs, label);
    TEST_ASSERT_EQUAL_MESSAGE(500, stubTones()[2].durationMs, label);
    TEST_ASSERT_EQUAL_MESSAGE(666, stubTones()[3].durationMs, label);
  }
}

void test_only_the_whole_label_names_the_pocket_calculator(void) {
  sound->playTune("POCKET CALCULATOR");

  TEST_ASSERT_EQUAL(16, stubTones().size());
  TEST_ASSERT_EQUAL(characterNote("P"), stubTones()[0].frequency);
}

void test_a_stop_ends_the_tune_after_the_note_that_is_sounding(void) {
  const char* labels[] = {"HELLO", " POCKET CALCULATOR "};
  for (const char* label : labels) {
    stubTones().clear();
    stopSignal->clear();
    stubAfterDelay() = [] {
      if (stubTones().size() == 2) {
        stopSignal->raise(StopCause::OPERATOR);
      }
    };
    sound->playTune(label);

    TEST_ASSERT_EQUAL_MESSAGE(2, stubTones().size(), label);
  }
}

// --- how long it takes
// ---------------------------------------------------------

void test_the_estimate_of_a_tune_is_how_long_it_takes(void) {
  std::vector<String> labels = {
      "",
      "A",
      " HELLO ",
      "ABCDEFGHIJKLMNOP",
      "♡ €5.00 ☆",
      "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGH"};
  for (const char* label : CALCULATOR_LABELS) {
    labels.push_back(label);
  }
  for (const String& label : labels) {
    const unsigned long start = micros();
    sound->playTune(label);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(micros() - start, sound->tuneUs(label),
                                     label.c_str());
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_label_plays_the_note_of_each_character_it_has_one_for);
  RUN_TEST(test_the_notes_of_a_long_label_get_shorter_from_the_sixth);
  RUN_TEST(test_a_label_of_16_characters_plays_every_note_whole);
  RUN_TEST(test_the_pocket_calculator_labels_play_its_melody);
  RUN_TEST(test_only_the_whole_label_names_the_pocket_calculator);
  RUN_TEST(test_a_stop_ends_the_tune_after_the_note_that_is_sounding);
  RUN_TEST(test_the_estimate_of_a_tune_is_how_long_it_takes);
  return UNITY_END();
}
