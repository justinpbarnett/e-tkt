#pragma once

#include <Arduino.h>

#include "Configuration.h"
#include "StopSignal.h"

/**
 * Brightness levels the machine actually asks for, 0.0 dark to 1.0 full.
 *
 * Named because the bare fractions read as noise at the call site: 0.125 says
 * nothing, LIGHT_FAINT says the LED is meant to be barely visible. What each
 * one means in a given command -- working, finished, homing -- stays with the
 * caller; these only say how bright.
 */
constexpr float LIGHT_FULL = 1.0f;
constexpr float LIGHT_HALF = 0.5f;
constexpr float LIGHT_DIM = 0.2f;
constexpr float LIGHT_FAINT = 0.125f;

/**
 * @brief Controls an LED connected to a PWM pin.
 *
 * Brightness is a fraction, never a duty cycle. The number of PWM levels
 * behind that fraction is this class's own business and no caller may depend
 * on it -- ETKT used to write `i / 128.0f` in a fade loop, rebuilding the very
 * constant this class exists to hide, which meant changing the PWM range here
 * would have silently changed how that fade looked.
 *
 * Patterns live here for the same reason. A blink is three facts (how many,
 * how bright, how fast) and a fade is two, so they fit in an argument list;
 * as loops at the call site they were eight lines of a print routine that
 * nothing could check without watching the board.
 *
 * A stop ends either pattern early and leaves the LED dark. The patterns are
 * not part of any job -- the one that matters is the finish LED celebrating
 * a label that is already cut -- so they only ask whether a stop is up, and
 * ending one does not count as cutting the job short.
 */
class Light {
 private:
  uint8_t pin;
  // Not owned; see LabelMaker.cpp.
  const StopSignal* stop;

  /** @brief Turns a 0.0-1.0 fraction into a PWM value, clamping both ends. */
  int level(float brightness);

  /**
   * @brief Waits ms, then says whether a pattern may go on. Once a stop is up
   * it may not, and the LED has been turned off.
   */
  bool hold(int ms);

 public:
  Light(uint8_t pin, const StopSignal* stop);
  ~Light();
  void initialize();

  /** @brief Lights the LED. brightness is clamped into 0.0-1.0. */
  void on(float brightness);

  void off();

  /**
   * @brief Blinks the LED `times` times, then leaves it lit.
   *
   * Each cycle goes dark for offMs and then lit for onMs, in that order, so
   * the blink is visible even when the LED was already on. Blocking: it takes
   * times * (onMs + offMs) and returns with the LED at `brightness`, unless a
   * stop ends it at the end of a half cycle, dark.
   */
  void blink(int times, float brightness, int onMs, int offMs);

  /**
   * @brief Fades the LED from `from` down to dark over roughly overMs.
   *
   * Every PWM level between the two gets written, so the fade is smooth
   * rather than stepped, and the step count is what makes the duration
   * approximate: overMs is divided into a whole number of equal waits.
   * Blocking, and guaranteed to end dark, so no off() is needed afterwards. A
   * stop goes straight to dark at the end of the step it lands in.
   */
  void fadeOut(float from, int overMs);
};
