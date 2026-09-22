#pragma once

#include <Arduino.h>

#include "Configuration.h"
#include "Drivers.h"
#include "Light.h"
#include "Logger.h"
#include "PressGeometry.h"

// Stock E-TKT geometry, restored 2026-09-22. The P_press is press-fit onto the
// servo's splined hub, so the printed part -- not the firmware -- defines where
// rest is. Assembly order matters and is easy to get backwards: power the board
// FIRST so the servo homes to REST_ANGLE, and only then push the P_press on so
// its lateral line sits 100% vertical (docs/diy/assembly/04_servo.md). Fitting
// the part first and teaching angles afterwards produces a stroke roughly twice
// as long as the design intends.
//
// docs/diy/calibration.md gives the one hard check on this: at power-on the
// press should sit 2mm from the I_nema_wheel_hub. If it does not, the fix is a
// re-fit of the P_press, not a different number here.
//
// LOW angle drives the press INTO the daisy wheel, HIGH swings it clear.
//
// REST_ANGLE: press clear of the wheel, P_press lateral line vertical here.
// STAMP_ANGLE: press just touching the wheel, measured on the bench.
#define REST_ANGLE 50
#define STAMP_ANGLE 15
#define PRESS_BITE_AT_MAX_FORCE 8  // degrees past STAMP_ANGLE at force 9

// The alignment test (press(..., slow=true), the SETUP "test" button) parks at
// the peak this long instead of PRESS_DWELL_MS, so the gap between press and
// daisy wheel can actually be looked at. Force 1 to 9 is only
// PRESS_BITE_AT_MAX_FORCE degrees of rotation -- a millimetre or two at the
// pad -- which is impossible to judge on a moving arm, so the useful test is
// a still one.
//
// This is eight times the normal dwell, and holding the press against the
// wheel means the servo is drawing stall current the whole time, which is the
// load that wears an MG996R's nylon gears and reams the printed P_press
// splines. It is only safe because the one caller that passes slow=true
// presses at CALIBRATION_VALUE_MIN, where the press barely touches. Anything
// that starts holding a real force here needs this number back down.
#define PRESS_TEST_DWELL_MS 2000

// Settle time at the peak and at rest on the normal printing path.
#define PRESS_DWELL_MS 250
#define PRESS_SETTLE_STEP_MS 50

// How long the ramp waits between one degree and the next.
//
// QUICK is the printing path: every degree still gets written, as fast as the
// loop can issue them. STRONG eases through each one, which is what cutting
// uses so its three repeated presses land the same way each time. SLOW is the
// calibration crawl.
//
// These also decide the real time spent at the peak, which is longer than the
// dwell above: the ramp in writes the peak and then waits a step before
// returning, and the ramp out writes the peak again before its first step, so
// the servo is commanded to hold for dwell + 2 * step. At QUICK that is
// exactly PRESS_DWELL_MS. At SLOW it is PRESS_TEST_DWELL_MS + 200.
// test/test_press pins both.
#define PRESS_STEP_QUICK_MS 0
#define PRESS_STEP_STRONG_MS 4
#define PRESS_STEP_SLOW_MS 100

/**
 * @brief Controlls a press connected to a servo.
 */
class Press {
 private:
  uint8_t pin;
  Logger* logger;
  // Not owned. The composition root in LabelMaker.cpp builds it and
  // outlives every module, so nothing here deletes it.
  ServoDriver* servo;
  Light* pressLed;

  /**
   * @brief Drives the servo from one angle to another one degree at a time,
   * pausing stepMs between degrees. A stepMs of 0 still writes every degree,
   * just as fast as the loop can issue them.
   */
  void sweep(int fromAngle, int toAngle, int stepMs);

  /**
   * @brief Repeatedly writes the same angle for holdMs so the servo has time
   * to actually arrive before anything else happens.
   */
  void settle(int angle, int holdMs);

 public:
  Press(Logger* logger, uint8_t pin, Light* pressLed, ServoDriver* servo);
  ~Press();

  /**
   * @brief Initializes the press.
   */
  void initialize();

  /**
   * @brief Drives the press into the daisy wheel and back out to rest.
   *
   * force (1-9, clamped) is the only argument that changes how far in the
   * press travels: PRESS_BITE_AT_MAX_FORCE degrees past STAMP_ANGLE at 9,
   * none at 1.
   *
   * strong and slow only change how quickly it gets there. strong=true eases
   * through each degree, which is what cutting uses so its three repeated
   * presses land the same way each time; slow=true is the calibration crawl,
   * and is also what holds at the peak for PRESS_TEST_DWELL_MS. Neither one
   * reaches any further in than force alone does.
   */
  void press(bool strong, int force, bool slow);

  /**
   * @brief Moves the press to the rest position, away from the daisy wheel
   */
  void rest();

  /**
   * @brief Bench calibration only: drives the servo to an arbitrary angle and
   * leaves it there. press() can only ever reach the band between STAMP_ANGLE
   * and PRESS_BITE_AT_MAX_FORCE degrees past it, so finding the angle at which
   * the press actually meets the daisy wheel needs a path that ignores those
   * constants.
   */
  void hold(int angle);

  /**
   * @brief Bench calibration only: stops driving the servo so the horn can be
   * back-driven by hand. A hobby servo has no position feedback, so nothing
   * can be read back from a horn posed this way -- it is for deciding the
   * geometry, not for measuring it.
   *
   * Prefer this over forcing a live servo: back-driving one that is actively
   * holding position strips its gears.
   */
  void release();

  /**
   * @brief Resumes driving the servo after release().
   */
  void engage();
};
