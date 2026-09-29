#pragma once

// Every stroke of the press, and where the daisy wheel stood for it.
//
// A FakeServo records angles and a FakeStepper records steps, and neither
// knows about the other. What comes out on the tape depends on the two
// together: which slot was under the press when it came down, and how far in
// it went. So this listens to the servo and asks the magnet where the wheel
// is.

#include <stdlib.h>

#include <vector>

#include "Configuration.h"
#include "FakeDrivers.h"
#include "FakeMagnet.h"

/**
 * @brief One stroke of the press, from leaving rest to getting back there.
 */
struct Stroke {
  // Where the wheel stood as the press came down. See FakeMagnet::bearing().
  long bearing;
  // The angle farthest from REST_ANGLE, which is how far in the press went.
  int deepest;
};

class StrokeLog {
 private:
  bool pressing = false;

 public:
  std::vector<Stroke> strokes;

  /**
   * @brief Listens to the servo from now on. Both have to outlive every
   * write the servo is asked for.
   */
  StrokeLog(FakeServo* servo, const FakeMagnet* magnet) {
    servo->afterWrite = [this, servo, magnet] {
      const int angle = servo->calls.back().value;
      if (angle == REST_ANGLE) {
        this->pressing = false;
        return;
      }
      if (!this->pressing) {
        this->pressing = true;
        Stroke stroke = {magnet->bearing(), angle};
        this->strokes.push_back(stroke);
        return;
      }
      Stroke& stroke = this->strokes.back();
      if (abs(angle - REST_ANGLE) > abs(stroke.deepest - REST_ANGLE)) {
        stroke.deepest = angle;
      }
    };
  }
};
