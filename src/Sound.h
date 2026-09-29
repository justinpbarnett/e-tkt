#pragma once

#include <Arduino.h>

#include "Configuration.h"
#include "StopSignal.h"

#define NOTE_DURATION_MAX 100
#define NOTE_DURATION_MIN 20
#define NOTE_DURATION_DECREASE 2

/**
 * @brief Controls the buzzer to play single notes, melodies, and
 * songs based on a label.
 */
class Sound {
 private:
  StopSignal* stop;

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
   * @brief Plays a tune on the buzzer based on the label text.
   *
   * A stop ends it after the note that is sounding.
   */
  void playLabel(String label);

  /**
   * @brief Plays a melody on the buzzer.
   * @details ♪ By pressing down a special key ♪
   *          ♪ It plays a little melody ♪
   *
   * A stop ends it after the note that is sounding.
   */
  void playMelody(String notes, String durations);
};
