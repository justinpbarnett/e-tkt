#pragma once

#include <stdint.h>

// Whether something is so, and since when.
//
// Pure arithmetic, no Arduino, so the host test suite can reach it. The link
// supervisor asks this of seven things, from a try that is under way to an
// address that is on the screen. Each was a flag and a time kept side by
// side, set together in one place and compared by hand in another.
//
// The times are millis(), which starts over every seven weeks. The lengths
// are counted by subtraction, so one that runs across that moment is still
// right.
class TimedFlag {
 private:
  bool raised = false;
  uint32_t sinceMs = 0;

 public:
  /** @brief It is so, from now. Set again, it counts from the later time. */
  void set(uint32_t nowMs) {
    this->raised = true;
    this->sinceMs = nowMs;
  }

  /** @brief It is so no longer. */
  void clear() { this->raised = false; }

  bool isSet() const { return this->raised; }

  /** @brief It is so, and has been for less than this long. */
  bool forLessThan(uint32_t nowMs, uint32_t lengthMs) const {
    return this->raised && nowMs - this->sinceMs < lengthMs;
  }

  /** @brief It is so, and has been for this long or longer. */
  bool forAtLeast(uint32_t nowMs, uint32_t lengthMs) const {
    return this->raised && nowMs - this->sinceMs >= lengthMs;
  }
};
