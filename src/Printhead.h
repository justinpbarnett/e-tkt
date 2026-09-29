#pragma once

#include <Arduino.h>

#include "Calibration.h"
#include "DaisyWheel.h"
#include "Logger.h"
#include "Press.h"
#include "StopSignal.h"

/**
 * @brief The daisy wheel and the press, worked together: everything that
 * comes down on the tape.
 *
 * A character, the cut and the test press are each a turn of the wheel and
 * then a press, unless a stop comes in between. The press never comes down
 * on a slot the wheel did not reach.
 */
class Printhead {
 private:
  Logger* logger;
  DaisyWheel* daisywheel;
  Press* press;
  StopSignal* stop;

  /**
   * @brief What every turn of the wheel comes back through. A wheel that
   * found no magnet cannot say which slot is under the press, so it stops
   * the job, with the wheel as the cause.
   *
   * @return the turn, unchanged.
   */
  Turn stopIfLost(Turn turn);

 public:
  Printhead(Logger* logger, DaisyWheel* daisywheel, Press* press,
            StopSignal* stop);

  /**
   * @brief Starts the press, lifted clear, and then the wheel, which homes
   * at the calibration's align and lets go of its coils.
   *
   * The boot home is for reference only: every character homes again
   * before it counts from anywhere, so a wheel that stays lost here is not
   * yet a job that fails, and raises no stop.
   */
  void initialize(const Calibration& calibration);

  /**
   * @brief Finds the magnet and turns the home character under the press.
   *
   * A stop halts the wheel within a step, wherever it has got to.
   *
   * This and every call below that turns the wheel homes first. One that
   * finds no magnet presses nothing and stops the job, with
   * StopCause::LOST_WHEEL, and the stop counts as obeyed.
   */
  void home(const Calibration& calibration);

  /**
   * @brief Turns the wheel so a slot is under the press, and presses
   * nothing.
   *
   * If the wheel does not carry the slot the log says so. A wheel parked
   * somewhere other than the slot that was asked for is worth hearing
   * about, and the other reasons it can be, a stop or a lost wheel, have
   * said so already.
   */
  void turnTo(const String& slot, const Calibration& calibration);

  /**
   * @brief Presses one character of a label into the tape.
   *
   * The wheel turns to the character's slot and the press comes down once,
   * at the calibration's force. A stop that comes while the wheel turns, or
   * as it gets there, keeps the press up. A space presses nothing and leaves
   * the wheel where it was.
   */
  void stamp(const String& character, const Calibration& calibration);

  /**
   * @brief Cuts the tape: the cut mark, pressed three times as hard as the
   * calibration's force.
   *
   * A stop is obeyed between the three presses, so it can leave the tape
   * partly cut. If the wheel does not reach the cut mark nothing is
   * pressed, and a wheel with no cut mark is logged.
   */
  void cut(const Calibration& calibration);

  /**
   * @brief The align test: one slow, light press of the M, held so the
   * operator can see whether it lands centred on the letter.
   *
   * Always at the minimum force, whatever the calibration's force says.
   */
  void testPress(const Calibration& calibration);

  /**
   * @brief Lifts the press clear of the wheel. Before the tape moves or the
   * wheel turns, since either one against a lowered press drags.
   */
  void rest();

  /**
   * @brief Puts the printhead away at the end of a job: the press up, and
   * the wheel's coils off.
   *
   * A wheel let go can be turned by hand, so the next character homes
   * before it counts from anywhere.
   */
  void park();
};
