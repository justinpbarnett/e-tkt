#pragma once

#include <Arduino.h>

#include "ETKT.h"
#include "Logger.h"

/**
 * @brief The button on the machine, and what a press of it does.
 *
 * One tact switch, which shorts its pin to ground. It works the machine with
 * no phone and no network to reach it over:
 *
 *   - While a job runs, a press stops it, as the panel's stop does. The stop
 *     is asked for as the button goes down.
 *   - While nothing runs, a press prints the last run again: see
 *     ETKT::repeat(). The run starts as the button comes up.
 *   - While nothing runs, holding the button for BUTTON_HOLD_MS unloads the
 *     roll. The machine then asks for the next roll on its screen, and a
 *     press loads it, in place of printing.
 *
 * So a roll is changed with a hold and two presses: unload, load, and the
 * run again.
 *
 * Two things are not taken for a press of an idle machine. A reading that
 * has not held for BUTTON_DEBOUNCE_MS is no press at all. And a press that
 * comes down before the machine has sat idle for BUTTON_ARMING_MS starts
 * nothing: see both in Configuration.h.
 *
 * It keeps no lock. poll() is for one task only, and everything it asks of
 * the job runner is safe to ask from a task other than the one the jobs run
 * on.
 */
class Button {
 private:
  uint8_t pin;
  ETKT* etkt;
  Logger* logger;

  // The pin as it was last read, and when that reading began.
  bool readingDown = false;
  unsigned long readingSinceMs = 0;

  // The button once a reading has held: down or up, and when it went down.
  bool down = false;
  unsigned long downSinceMs = 0;

  // Whether the press under way has done all it is going to: it stopped a
  // job, it was not meant for an idle machine, or it has been held and has
  // unloaded the roll.
  bool spent = false;

  // When the machine was last busy, or the button started, and whether the
  // machine has sat idle for BUTTON_ARMING_MS since. Both, so that the
  // answer still holds when millis() wraps.
  unsigned long idleSinceMs = 0;
  bool armed = false;

  /**
   * @brief The button has gone down, with the machine `busy` or not.
   */
  void pressed(bool busy);

  /**
   * @brief The button has come up again.
   */
  void released();

  /**
   * @brief The button has been down for BUTTON_HOLD_MS.
   */
  void held();

 public:
  /**
   * @param pin the pin the switch shorts to ground.
   */
  Button(uint8_t pin, ETKT* etkt, Logger* logger);

  /**
   * @brief Pulls the pin up and starts the button's clocks. Once, before the
   * first poll().
   *
   * A button that is down already starts nothing, however long it stays
   * down: holding it through the boot is how the saved network is cleared.
   */
  void initialize();

  /**
   * @brief Reads the button, and does what a press of it asks for.
   *
   * Every BUTTON_POLL_MS, from one task, and not the task the jobs run on: a
   * press is to stop a job while that task is busy running it.
   */
  void poll();
};
