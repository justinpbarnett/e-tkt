#pragma once

#include <mutex>

#include "Arduino.h"
#include "CharacterSet.h"
#include "Configuration.h"
#include "Drivers.h"
#include "HallSwitch.h"
#include "Logger.h"
#include "Motion.h"
#include "StopSignal.h"

// How often homing looks at the hall sensor while it searches for the
// magnet, and so how often the tape feeding meanwhile gets a turn.
constexpr unsigned long HOME_SWEEP_POLL_US = 100;

// How long the wheel is given to come to rest once it is home, and once it
// has turned to a slot, before anything else happens.
constexpr unsigned long HOME_SETTLE_MS = 100;
constexpr unsigned long TURN_SETTLE_MS = 25;

/**
 * @brief How a turn of the daisy wheel ended.
 */
enum class Turn {
  // The slot asked for is under the press.
  REACHED,
  // The wheel does not carry the slot asked for, so it did not turn.
  NO_SLOT,
  // A stop halted the wheel, wherever it had got to.
  STOPPED,
  // Homing swept for the magnet and did not find it, so nothing counted
  // from where the search gave up can be trusted.
  LOST,
};

/**
 * @brief Controls the daisy wheel stepper motor and computes the geometry
 * necessary for moving it into the correct position.
 *
 * The tape feeds while the wheel turns: every loop and wait in here keeps
 * the background going, so neither motor waits on the other.
 */
class DaisyWheel {
 private:
  Logger* logger;
  HallSwitch* hall;
  // Not owned; see LabelMaker.cpp.
  StepperDriver* stepper;
  StopSignal* stop;
  // What runs while the wheel turns: the tape, feeding up to the next
  // character.
  Background* background;
  const int stepsPerRevolution = CHAR_STEP_COUNT * CHAR_MICROSTEPS;
  const float stepsPerChar = (float)stepsPerRevolution / WHEEL_SLOT_COUNT;
  // How far homing searches for the magnet before it gives up: a turn and a
  // half.
  const long searchSteps = stepsPerRevolution * 3 / 2;
  // -1 is "no slot under the press", which is what deenergize() leaves
  // behind and what move() treats as "nowhere to count from, home first".
  // Every sibling here carries a starting value; this one did not, so
  // before home() ran it held whatever the allocation had in it.
  int currentChar = -1;

  // What homeUs() and moveUs() have worked out so far, by slot, since the
  // board takes a while over each one: 0 is not worked out yet. The homes
  // are at estimatedAlign's align. The webserver's task asks for estimates
  // while the command loop may be asking for its own, hence the lock.
  mutable std::mutex estimateLock;
  mutable int estimatedAlign = 0;
  mutable unsigned long homeFromSlotUs[WHEEL_SLOT_COUNT] = {};
  mutable unsigned long turnToSlotUs[WHEEL_SLOT_COUNT] = {};

  /**
   * @brief Forgets where the wheel is, after a stop or a search that found
   * no magnet left it somewhere unknown. The next move() homes before it
   * counts from here.
   */
  void lose();

  /**
   * @brief Where the align puts the J, in steps from where the magnet was
   * found.
   */
  long alignPosition(int align) const;

  /**
   * @brief Where a slot `slots` on from the J is, in steps from the J.
   */
  long slotPosition(int slots) const;

  // homeUs() and the turn part of moveUs(), from and to a slot, with
  // estimateLock held.
  unsigned long homeFromUs(int slot, int align) const;
  unsigned long turnToUs(int slot) const;

 public:
  DaisyWheel(Logger* logger, HallSwitch* hall, StepperDriver* stepper,
             StopSignal* stop, Background* background);
  ~DaisyWheel();

  /**
   * @brief Starts the hall sensor and sets up the stepper. The wheel does
   * not move: the first home() is the caller's, at the align it wants.
   */
  void initialize();

  /**
   * @brief Finds the magnet and turns the home character, the J, under the
   * press, offset by the align.
   *
   * REACHED once the J is there. LOST if a turn and a half of searching
   * never showed the magnet to the hall sensor. STOPPED if a stop halted
   * the wheel first, within a step, wherever it had got to. Either of the
   * last two leaves the wheel unreferenced.
   */
  Turn home(int align);

  /**
   * @brief Turns the slot for character "c" under the press, homing first
   * unless it is there already.
   *
   * Anything but REACHED means the wheel did not get there, and the press
   * must not come down: NO_SLOT if the wheel does not carry the character,
   * and otherwise however the homing or the turn after it ended.
   */
  Turn move(String c, int alignFactor);

  /**
   * @brief How long home(align) takes with the wheel at the slot for
   * `from`: the search for the magnet, the align, and the wait after.
   *
   * Worked out, not timed, and kept once it is, since the board takes a
   * while over it. It takes the slot to be clear of the magnet. A wheel
   * homed at a high align can leave the J in front of it, and a home from
   * there first steps off the magnet, which this does not count. A wheel at
   * no slot, one that has lost its place, is counted as at the J.
   *
   * Safe to call from another task while the wheel turns: it reads nothing
   * the turning changes.
   */
  unsigned long homeUs(const String& from, int align) const;

  /**
   * @brief How long move(to, align) takes with the wheel at the slot for
   * `from`, as homeUs() works it out: none if the slot is under the press
   * already, or if the wheel has no slot for `to`.
   */
  unsigned long moveUs(const String& from, const String& to, int align) const;

  /**
   * @brief Deactivates the daisy wheel stepper motor, potentially losing its
   * position.
   */
  void deenergize();
};
