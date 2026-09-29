#pragma once

#include <atomic>

/** @brief What asked for a stop. */
enum class StopCause {
  // Nothing has.
  NONE,
  // The operator, with the stop button.
  OPERATOR,
  // The daisy wheel, which turned without finding its magnet. Nothing it
  // turned to from there would have put the right slot under the press.
  HOMING,
};

/**
 * @brief A request to stop what the machine is doing, now.
 *
 * The operator raises it from the webserver's task, which goes on answering
 * requests while the command loop is busy with the motors, and the command
 * loop raises it itself when the daisy wheel is lost. It is read by the loop
 * everywhere the work can be dropped: between one step of a stepper and the
 * next, one character and the next, one note of a tune and the next. The
 * press is the exception. Once the arm is on its way down it finishes the
 * stroke, because a servo stopped partway is a press held against the wheel.
 *
 * Reading it is not the same as obeying it, so there are two ways to read
 * it. shouldStop() is for the code that drops work when it says yes, and it
 * remembers that it did. raised() is for anything that only wants to know.
 * That difference is what lets the command loop tell a job that was cut
 * short from one that had already finished when the stop arrived.
 *
 * Either cause is raised only while a command is running: the operator's by
 * ETKT under its lock, and the wheel's from inside the command itself. ETKT
 * clears it under the lock when the command is over, so a stop can never
 * outlive the job it was meant for.
 */
class StopSignal {
 private:
  // The StopCause, NONE while the signal is down. Crosses from the
  // webserver's task to the command loop, which may run on the other core,
  // and is a whole word so that every target this builds for can
  // compare-and-swap it natively.
  std::atomic<int> why;

  // Whether anything has dropped work because of this stop. Only the command
  // loop reads or writes it.
  bool honoured;

 public:
  StopSignal() : why((int)StopCause::NONE), honoured(false) {}

  /**
   * @brief Asks for a stop. Safe from any task.
   *
   * The first cause stands until clear(): a stop raised while another is
   * already up changes nothing, because the first is what ended the job.
   */
  void raise(StopCause cause) {
    int none = (int)StopCause::NONE;
    this->why.compare_exchange_strong(none, (int)cause);
  }

  /** @brief Whether a stop has been asked for. Reading it changes nothing. */
  bool raised() const { return this->cause() != StopCause::NONE; }

  /** @brief What asked for the stop, or NONE while it is down. */
  StopCause cause() const { return (StopCause)this->why.load(); }

  /**
   * @brief Whether to drop the work in hand. True once a stop is raised, and
   * from then on the job counts as cut short -- so ask only at a point that
   * does stop when the answer is yes.
   */
  bool shouldStop() {
    if (!this->raised()) {
      return false;
    }
    this->honoured = true;
    return true;
  }

  /** @brief Whether any shouldStop() has said yes since the last clear(). */
  bool cutShort() const { return this->honoured; }

  /** @brief Puts it back down, ready for the next job. */
  void clear() {
    this->why.store((int)StopCause::NONE);
    this->honoured = false;
  }
};
