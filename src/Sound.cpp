#include "Sound.h"

#include <Arduino.h>
#include <ESP32Tone.h>

#include <vector>

#include "CharacterSet.h"
#include "Configuration.h"
#include "Melody.h"
#include "StopSignal.h"
#include "Utility.h"

Sound::~Sound() {}

Sound::Sound(StopSignal* stop) { this->stop = stop; }

void Sound::initialize() { pinMode(BUZZER_PIN, OUTPUT); }

void Sound::play(int frequency, int duration) {
  if (ENABLE_SOUND) {
    tone(BUZZER_PIN, frequency, duration);
  } else {
    delay(duration);
  }
}

void Sound::play(String character, int duration) {
  auto frequency = characterNote(character);
  if (frequency == 0) {
    // The character isn't on the wheel, so just ignore it.
    return;
  }
  this->play(frequency, duration);
}

// The melody the pocket calculator labels play, from Kraftwerk's song about
// one, and the length of each of its notes: see melodyNoteMs(). The cut mark
// has no note, and is skipped.
static const char* const CALCULATOR_NOTES =
    "*4599845887*459984588764599845887*4599845887";
static const char* const CALCULATOR_DURATIONS =
    "88843888484888438884848884388848488843888484";

static bool namesThePocketCalculator(const String& label) {
  return label == " TASCHENRECHNER " || label == " POCKET CALCULATOR " ||
         label == " DENTAKU " || label == " CALCULADORA " ||
         label == " MINI CALCULATEUR ";
}

std::vector<Sound::Note> Sound::tune(const String& label) const {
  std::vector<Note> notes;
  if (namesThePocketCalculator(label)) {
    const std::vector<String> characters =
        Utility::characters(CALCULATOR_NOTES);
    for (size_t i = 0; i < characters.size(); i++) {
      const int frequency = characterNote(characters[i]);
      if (frequency != 0) {
        notes.push_back({frequency, melodyNoteMs(CALCULATOR_DURATIONS, i), 0});
      }
    }
    return notes;
  }

  // Any other label plays its own characters, one note each.
  const std::vector<String> characters = Utility::characters(label);
  const int length = characters.size();
  for (int i = 0; i < length; i++) {
    int duration;
    // If the label is over 16 characters, decrease the note duration every
    // character starting at character 5.
    if (length > 16 && i > 4) {
      duration = max(NOTE_DURATION_MAX - (i - 4) * NOTE_DURATION_DECREASE,
                     NOTE_DURATION_MIN);
    } else {
      duration = NOTE_DURATION_MAX;
    }
    // A character that is not on the wheel has no note, only the silence.
    notes.push_back({characterNote(characters[i]), duration, duration / 2});
  }
  return notes;
}

void Sound::playTune(const String& label) {
  for (const Note& note : this->tune(label)) {
    if (this->stop->shouldStop()) {
      return;
    }
    if (note.frequency != 0) {
      this->play(note.frequency, note.ms);
    }
    if (note.restMs > 0) {
      delay(note.restMs);
    }
  }
}

unsigned long Sound::tuneUs(const String& label) const {
  unsigned long ms = 0;
  for (const Note& note : this->tune(label)) {
    ms += (note.frequency != 0 ? note.ms : 0) + note.restMs;
  }
  return ms * 1000UL;
}
