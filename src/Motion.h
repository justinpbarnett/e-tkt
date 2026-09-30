#pragma once

#include "Arduino.h"
#include "Drivers.h"
#include "StopSignal.h"

// Moving a stepper in a way a stop can interrupt, and keeping a second one
// going meanwhile.
//
// AccelStepper's own runToNewPosition() is a loop of run() calls that returns
// only once the motor arrives, and while it runs nothing else on the command
// loop gets a look in. This is the same loop with a stop check in it, and a
// turn for the motor in the background.

/**
 * @brief A motor that carries on while the command loop does something else.
 *
 * Nothing on the board turns a motor for you: AccelStepper steps only when
 * run() is called, so a motor left to itself stops. One that moves in the
 * background is one that every loop and every wait calls keepGoing() for, as
 * often as it can, until it is done. The tape feeds this way while the daisy
 * wheel turns to the next character. See Feeder.
 *
 * Anything that holds the command loop up for longer than a step, a delay()
 * or a redraw of the screen, stalls the motor for as long, and a motor
 * stalled at speed can lose steps. So a loop that keeps one going waits with
 * pause(), and the screen is drawn only once the motor is done.
 */
class Background {
 public:
  virtual ~Background() {}

  /**
   * @brief Takes the next step, or starts the next part of the job, if
   * either is due, and returns at once whether or not it was.
   */
  virtual void keepGoing() = 0;

  /**
   * @brief Keeps going until the job is done, and returns then.
   */
  virtual void finish() = 0;
};

// How often a loop that keeps a motor going comes round on the board: the
// stop check, the run() that decides whether a step is due, and the yield().
// A few microseconds, and this is the guess. Nothing waits on it; only the
// estimates read it. The host's yield() takes exactly this long, so on the
// host they come out exact.
constexpr unsigned long MOTOR_LOOP_US = 5;

/**
 * @brief How long a wait of `us` takes in a loop that looks at the clock
 * every `loopUs`: until the first look at or after it.
 */
inline unsigned long waitedUs(unsigned long us, unsigned long loopUs) {
  return (us + loopUs - 1) / loopUs * loopUs;
}

/**
 * @brief How long a stepper takes over a move, worked out rather than timed,
 * so a job can say how long it will be before it starts.
 *
 * AccelStepper has no formula for it. Each step's interval comes from the
 * one before, by the recurrence in its computeNewSpeed(), and the same lines
 * decide when to slow down, which near the end of a move can leave the motor
 * crawling a step at its starting pace. So this runs the same recurrence, in
 * the library's own types, and adds the intervals up. A loop that calls
 * run() sees that a step is due only the next time it comes round, so each
 * interval is rounded up to a whole number of turns of it.
 *
 * Every move here starts from rest, as each of the machine's does: none is
 * asked for until the one before it has arrived.
 */
struct StepperTiming {
  // What the motor is set up with, in steps/s and steps/s^2.
  float maxSpeed;
  float acceleration;

  /**
   * @brief How long after a step the first step of a new move can come. A
   * move asked for sooner waits this long from the step before; one asked
   * for later takes its first step at once.
   */
  unsigned long firstStepUs() const;

  /**
   * @brief How long a move of `distance` steps takes with run() called every
   * `loopUs`, from its first step to the step it arrives on.
   */
  unsigned long moveUs(long distance, unsigned long loopUs) const;

  /**
   * @brief How long the same move takes from its first step to its
   * `steps`th, or to the step it arrives on if that comes first.
   */
  unsigned long stepsUs(long distance, long steps, unsigned long loopUs) const;
};

/**
 * @brief delay(), keeping a motor in the background going meanwhile.
 */
inline void pause(unsigned long ms, Background* background) {
  const unsigned long start = micros();
  while (micros() - start < ms * 1000UL) {
    background->keepGoing();
    yield();
  }
}

/**
 * @brief Halts a stepper where it is, at once.
 *
 * Renaming where the motor is as where it is going zeroes its speed, which
 * is AccelStepper's abrupt stop. AccelStepper::stop() is the gentle one: it
 * sets a target far enough ahead to slow down in, so the motor goes on
 * turning for as long as it took to speed up.
 */
inline void halt(StepperDriver* stepper) {
  stepper->setCurrentPosition(stepper->currentPosition());
}

/**
 * @brief Runs a stepper to an absolute position, unless a stop comes first,
 * keeping the background going on the way.
 *
 * The motor accelerates and decelerates exactly as it does under
 * AccelStepper::runToNewPosition(), and the stop is checked before every
 * step, so it is obeyed within one step of being raised. Returns true if the
 * motor arrived, false if the stop halted it on the way, in which case it is
 * left wherever it had got to and still energised. The background is left
 * to see the stop for itself, the next time it is kept going.
 */
inline bool runToNewPosition(StepperDriver* stepper, long position,
                             StopSignal* stop, Background* background) {
  stepper->move(position - stepper->currentPosition());
  for (;;) {
    if (stop->shouldStop()) {
      halt(stepper);
      return false;
    }
    if (!stepper->run()) {
      return true;
    }
    background->keepGoing();
    yield();
  }
}
