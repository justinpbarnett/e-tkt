#pragma once

#include <atomic>

/**
 * @brief The operator's request to stop what the machine is doing, now.
 *
 * Raised from the webserver's task, which goes on answering requests while
 * the command loop is busy with the motors, and read by the command loop
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
 * ETKT raises it only while a command is running and clears it when the
 * command is over, both under its lock, so a stop can never outlive the job
 * it was meant for.
 */
class StopSignal {
 private:
  // Crosses from the webserver's task to the command loop, which may run on
  // the other core. Only load() and store(): they are all this needs, and
  // they are the operations every target this builds for does natively.
  std::atomic<bool> up;

  // Whether anything has dropped work because of this stop. Only the command
  // loop reads or writes it.
  bool honoured;

 public:
  StopSignal() : up(false), honoured(false) {}

  /** @brief Asks for a stop. Safe from any task. */
  void raise() { this->up.store(true); }

  /** @brief Whether a stop has been asked for. Reading it changes nothing. */
  bool raised() const { return this->up.load(); }

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
    this->up.store(false);
    this->honoured = false;
  }
};
