#pragma once

#include <Arduino.h>

// The one definition of how long a note in a melody lasts.
//
// Pure arithmetic, no buzzer and no ESP32Tone, so the host test suite can
// reach it. It used to sit inline in Sound::playMelody as
// `2000 / atoi(&charDuration)`, where charDuration was a single char on the
// stack. atoi wants a string, so it read whatever the frame held after that
// byte, and a parse that came back 0 turned the line into an integer division
// by zero -- an exception on the Xtensa, and a reboot mid-melody.

// What a digit divides. "4" is a quarter of this, "8" an eighth.
constexpr int MELODY_WHOLE_NOTE_MS = 2000;

/**
 * @brief How long the note at `index` lasts, in milliseconds.
 *
 * @param durations one digit per note, in the order the notes are played.
 *        Each digit is how many of that note fill MELODY_WHOLE_NOTE_MS, so
 *        "8" is an eighth note and "4" a quarter. ASCII only: the caller
 *        counts notes in UTF-8 code points, and this index only lines up with
 *        that count while every duration is one byte.
 * @param index which note, counting from 0.
 *
 * Returns the whole note for anything that is not a digit 1-9, and for an
 * index outside `durations`. Both mean the melody's two strings disagree,
 * which is a typo in the melody rather than something the buzzer can fix, and
 * a two-second note says so audibly. The one thing it will not do is divide
 * by zero.
 */
inline int melodyNoteMs(const String& durations, int index) {
  if (index < 0 || index >= (int)durations.length()) {
    return MELODY_WHOLE_NOTE_MS;
  }
  const int notesPerWhole = durations[index] - '0';
  if (notesPerWhole < 1 || notesPerWhole > 9) {
    return MELODY_WHOLE_NOTE_MS;
  }
  return MELODY_WHOLE_NOTE_MS / notesPerWhole;
}
