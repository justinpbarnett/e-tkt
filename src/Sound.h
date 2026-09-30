#pragma once

#include <Arduino.h>

#include <vector>

#include "Configuration.h"
#include "StopSignal.h"

#define NOTE_DURATION_MAX 100
#define NOTE_DURATION_MIN 20
#define NOTE_DURATION_DECREASE 2

/**
 * @brief Controls the buzzer to play single notes, and the tune a job opens
 * with.
 */
class Sound {
 private:
  StopSignal* stop;

  // One note of a tune, and the silence after it. A note with no frequency
  // is only the silence.
  struct Note {
    int frequency;
    int ms;
    int restMs;
  };

  /**
   * @brief The notes playTune(label) plays, in order.
   */
  std::vector<Note> tune(const String& label) const;

 public:
  Sound(StopSignal* stop);
  ~Sound();

  /**
   * @brief Starts and sets up the buzzer.
   */
  void initialize();

  /**
   * @brief Plays a note on the buzzer.
   */
  void play(int frequency = 2000, int duration = 1000);

  /**
   * @brief Plays a note on the buzzer based on its character.
   */
  void play(String character, int duration = 1000);

  /**
   * @brief Plays the tune a job opens with, from the label text: each
   * character's note, the notes shorter one by one on a long label.
   *
   * The labels that name the pocket calculator play the melody of
   * Kraftwerk's song about one instead. A stop ends the tune after the note
   * that is sounding.
   */
  void playTune(const String& label);

  /**
   * @brief How long playTune(label) takes, from the same notes: worked out,
   * not timed.
   */
  unsigned long tuneUs(const String& label) const;
};
