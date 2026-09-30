#pragma once

#include <Arduino.h>

#include "Calibration.h"
#include "DaisyWheel.h"
#include "Logger.h"
#include "Motion.h"
#include "Press.h"
#include "StopSignal.h"

/**
 * @brief The daisy wheel and the press, worked together: everything that
 * comes down on the tape.
 *
 * A character, the cut and the test press are each a turn of the wheel and
 * then a press, unless a stop comes in between. The press never comes down
 * on a slot the wheel did not reach, nor on tape that is still moving: the
 * tape feeds up to the character while the wheel turns to it, and the press
 * waits for both.
 */
class Printhead {
 private:
  Logger* logger;
  DaisyWheel* daisywheel;
  Press* press;
  StopSignal* stop;
  // The tape, which feeds in the background while the wheel turns.
  Background* tape;

  /**
   * @brief What every turn of the wheel comes back through. A wheel that
   * found no magnet cannot say which slot is under the press, so it stops
   * the job, with the wheel as the cause.
   *
   * @return the turn, unchanged.
   */
  Turn stopIfLost(Turn turn);

  /**
   * @brief Whether the press can come down now: the wheel reached the slot
   * asked for, and no stop came. Waits for the tape to arrive and settle
   * before it looks at the stop, so a stop that comes meanwhile keeps the
   * press up too.
   */
  bool readyToPress(Turn turn);

  /**
   * @brief How long a turn to `slot` and then a press take, with tapeUs of
   * feeding under way. The wheel turns while the tape feeds, and the press
   * waits for both.
   */
  unsigned long pressedUs(const String& from, const String& slot,
                          unsigned long tapeUs, bool strong,
                          const Calibration& calibration) const;

 public:
  Printhead(Logger* logger, DaisyWheel* daisywheel, Press* press,
            StopSignal* stop, Background* tape);

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
   * at the calibration's force. A stop that comes while the wheel turns, as
   * it gets there, or while the press waits for the tape, keeps the press
   * up. A space presses nothing, leaves the wheel where it was, and does not
   * wait for the tape.
   */
  void stamp(const String& character, const Calibration& calibration);

  /**
   * @brief Cuts the tape: the cut mark, pressed once as hard as the
   * calibration's force.
   *
   * The blade does not go all the way through, so the label still comes
   * off with scissors. If the wheel does not reach the cut mark nothing is
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

  /**
   * @brief How long home() takes for a parked wheel.
   *
   * Every job homes one first. Parked, the wheel has lost its place, so
   * this counts the longest search for the magnet there is: from the J,
   * which sits just past it.
   *
   * This and the estimates below are worked out from the same ramps and
   * waits the motors run, not timed. See DaisyWheel::moveUs() and
   * Feeder::feedUs() for the little they leave out.
   */
  unsigned long homeUs(const Calibration& calibration) const;

  /**
   * @brief How long a character takes as a label prints it: stamp() with
   * the wheel at `from` and tapeUs of feeding started just before, and then
   * the rest of the feeding.
   *
   * That is the longer of the turn and the tape, and then the press. A
   * space, like a character the wheel lacks, is only the tape. A wheel at
   * no slot counts as at the J.
   */
  unsigned long stampUs(const String& from, const String& character,
                        unsigned long tapeUs,
                        const Calibration& calibration) const;

  /**
   * @brief How long the cut takes as a label ends with it: cut() with the
   * wheel at `from` and tapeUs of feeding started just before, and then the
   * rest of the feeding.
   */
  unsigned long cutUs(const String& from, unsigned long tapeUs,
                      const Calibration& calibration) const;
};
