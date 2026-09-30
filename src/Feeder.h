#pragma once

#include <Arduino.h>

#include "Configuration.h"
#include "Drivers.h"
#include "Logger.h"
#include "Motion.h"
#include "StopSignal.h"

// The waits around feeding, all from the original firmware: the coils
// are given FEED_SETTLE_ON_MS to come up before the first step, the motor
// FEED_GAP_MS at the end of each feed, and the tape FEED_SETTLE_OFF_MS once
// the coils let go before anything else happens.
constexpr unsigned long FEED_SETTLE_ON_MS = 10;
constexpr unsigned long FEED_GAP_MS = 10;
constexpr unsigned long FEED_SETTLE_OFF_MS = 20;

// One feed: an eighth of a turn of the feed motor.
constexpr long STEPS_PER_FEED = FEED_MOTOR_STEPS_PER_REVOLUTION / 8;

/**
 * @brief Controls the feeder stepper motor.
 *
 * This motor feeds the label tape forward, and turns the other way only to
 * back the tape out of the cog when a roll is unloaded. It never needs to
 * hold its position.
 *
 * It feeds in the background: start() asks for feeds and returns at once,
 * and the tape moves as the command loop keeps it going, which it does from
 * every loop and wait of the daisy wheel's. So the wheel turns to the next
 * character while the tape moves up to it. feed() is the same thing for a
 * caller with nothing else to do, and waits for it.
 */
class Feeder : public Background {
 private:
  // Where the feeds asked for are up to. The coils are live from SETTLING_ON
  // through GAP, and free in SETTLING_OFF and IDLE.
  enum class Phase {
    IDLE,
    SETTLING_ON,
    MOVING,
    GAP,
    SETTLING_OFF,
  };

  Logger* logger;
  // Not owned; see LabelMaker.cpp.
  StepperDriver* stepper;
  StopSignal* stop;
  long fed = 0;
  Phase phase = Phase::IDLE;
  // Feeds asked for and not yet begun.
  int pending = 0;
  // micros() when the wait the phase is in began.
  unsigned long phaseStartUs = 0;
  // Where the feed under way began.
  long moveStart = 0;
  // Whether the feeds under way back the tape out, rather than feed it.
  bool backingOut = false;

  void enter(Phase phase);
  bool waited(unsigned long ms) const;

  /**
   * @brief With the coils up: begins the next feed, or lets go when there
   * is none.
   */
  void next();

  /**
   * @brief Halts the feed under way, if there is one, where it is, and
   * counts it if the tape moved at all.
   */
  void haltFeed();

  /**
   * @brief Counts the feed that just moved the tape, unless it backed the
   * tape out: see feeds().
   */
  void countFeed();

 public:
  Feeder(Logger* logger, StepperDriver* stepper, StopSignal* stop);
  ~Feeder();
  void initialize();

  /**
   * @brief Asks for `feeds` more feeds, after any still under way, and
   * returns without waiting for them. The motor is let go once they are
   * done.
   *
   * A stop halts the motor within a step and drops the feeds still to come.
   * One already up when this is called asks for nothing. With ENABLE_FEED
   * off nothing moves, and it waits half a second in place of the feeds.
   */
  void start(int feeds);

  /**
   * @brief Takes the next step of the feeds asked for, if one is due.
   *
   * The stop is looked at only while there is tape still to move, so a
   * stop that comes once the last feed has arrived cuts nothing short.
   */
  void keepGoing() override;

  /**
   * @brief Keeps going until every feed asked for is done, the motor is
   * free and the tape has settled, or a stop has ended them.
   */
  void finish() override;

  /**
   * @brief Pushes the tape forward `repeat` feeds, and leaves the motor
   * free: start() and finish() in one.
   */
  void feed(int repeat = 1);

  /**
   * @brief Turns the motor the other way for `feeds` feeds, so the tape
   * backs out of the cog, and waits for it: the feeds under way go on
   * forward first, and it returns with the motor free and the tape settled.
   *
   * A stop halts it as it halts a feed. With ENABLE_FEED off nothing moves,
   * and it waits half a second in place of the feeds.
   */
  void backOut(int feeds);

  /**
   * @brief How long feed(feeds) takes: from start() to the tape settled and
   * the motor free.
   *
   * Worked out, not timed. It counts the loop that keeps the motor going as
   * coming round every MOTOR_LOOP_US, which finish() and the daisy wheel's
   * turns and waits all do. Homing, which looks every HOME_SWEEP_POLL_US
   * while it searches, holds a feed under way back a little.
   */
  unsigned long feedUs(int feeds) const;

  /**
   * @brief Lets go of the motor at once, halting a feed under way and
   * dropping any still to come.
   */
  void deenergize();

  /**
   * @brief Every feed since power-on that actually moved the tape forward.
   *
   * The machine has no way to see the tape, so this count is the only
   * evidence of how much of the roll has been used. It is counted here,
   * where the motor turns, rather than by each command that asks for a feed:
   * four commands feed, and a count kept by the callers would be right only
   * until the next one was added. With ENABLE_FEED off nothing moves, so
   * nothing is counted. A feed a stop or a deenergize() cut short counts as a
   * whole one if the motor turned at all.
   *
   * backOut() leaves the count as it was. The tape it pulls back out of the
   * cog is still on the roll, but the machine cannot tell how much came
   * back: once the end is out of the cog, the motor turns without moving it.
   * Left alone, the roll reads a little shorter than it is rather than
   * longer.
   */
  long feeds() const;
};
