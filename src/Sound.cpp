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

void Sound::playLabel(String label) {
  // plays a music according to the label letters

  const std::vector<String> characters = Utility::characters(label);
  const int length = characters.size();

  for (int i = 0; i < length; i++) {
    if (this->stop->shouldStop()) {
      return;
    }
    int duration;
    // If the label is over 16 characters, decrease the note duration every
    // character starting at character 5.
    if (length > 16 && i > 4) {
      duration = max(NOTE_DURATION_MAX - (i - 4) * NOTE_DURATION_DECREASE,
                     NOTE_DURATION_MIN);
    } else {
      duration = NOTE_DURATION_MAX;
    }

    this->play(characters[i], duration);
    delay(duration / 2);
  }
}

void Sound::playMelody(String notes, String durations) {
  const std::vector<String> characters = Utility::characters(notes);

  for (size_t i = 0; i < characters.size(); i++) {
    if (this->stop->shouldStop()) {
      return;
    }
    auto frequency = characterNote(characters[i]);
    if (frequency == 0) {
      continue;
    }
    this->play(frequency, melodyNoteMs(durations.c_str(), i));
  }
}
