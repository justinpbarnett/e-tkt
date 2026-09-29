// Bench rig: teach the two press angles and log them.
//
//   pio run -e bench-servo -t upload && pio device monitor
//
// A hobby servo has no position feedback, so the lever cannot be posed by hand
// and read back. Inverted instead: the firmware sweeps and the tact switch on
// WIFI_RESET_PIN stops it. The result is two numbers for Machine.h, REST_ANGLE
// and STAMP_ANGLE; docs/diy/assembly/04_servo.md says when to fit the P_press
// relative to this.
//
// Deliberately silent. ESP32Servo and ESP32Tone both allocate LEDC channels,
// and a tone() that lands on the channel attach() took would strip the servo
// of its 50 Hz signal for the rest of the run -- which is indistinguishable at
// the bench from "the servo is dead". Rounds are signalled on the LEDs
// instead: character LED steady = round 1 (rest), both LEDs = round 2 (stamp).
//
// The angle is logged every 10 degrees so the sweep can be confirmed from the
// capture even when nothing appears to move on the bench, and every GPIO13
// transition is logged so a button that is never seen is obvious.
//
// Flash serial-upload afterwards to put the label maker back.

#include <Arduino.h>

#include "ArduinoDrivers.h"
#include "Configuration.h"
#include "Light.h"
#include "Logger.h"
#include "Press.h"
#include "StopSignal.h"

ArduinoServo* pressServo = new ArduinoServo();

Logger* logger = new Logger();
// Nothing raises it here. The modules take one because a job can stop them.
StopSignal* stopSignal = new StopSignal();
Light* ledChar = new Light(CHARACTER_LED_PIN, stopSignal);
Light* ledFinish = new Light(FINISH_LED_PIN, stopSignal);
Press* press = new Press(logger, SERVO_PIN, ledChar, pressServo);

void setup() {
  logger->initialize();
  ledFinish->initialize();
  ledChar->initialize();
  press->initialize();

  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
  const int sweepHi = 180, sweepLo = 0;
  int captured[2] = {-1, -1};
  int lastBtn = digitalRead(WIFI_RESET_PIN);
  logger->log(String("SERVOTEACH: GPIO13 at start = ") +
              (lastBtn ? "HIGH (released, good)" : "LOW (stuck/shorted)"));

  // Phase L. The horn cannot be posed by hand while the servo is driving it
  // -- forcing a live MG996R is how its nylon gears strip -- so cut the pulses
  // first and let it go slack. Nothing is measured here, because nothing can
  // be: this phase exists so the geometry can be chosen by feel before the
  // horn is committed to a spline.
  press->release();
  logger->log(
      "SERVOTEACH: LIMP phase -- servo released, push the press "
      "by hand and decide where it should sit. Tap when decided.");
  {
    bool flip = false;
    for (int t = 0; digitalRead(WIFI_RESET_PIN) == HIGH; t++) {
      if (t % 40 == 0) {
        flip = !flip;
        if (flip) {
          ledChar->on(LIGHT_FULL);
          ledFinish->off();
        } else {
          ledChar->off();
          ledFinish->on(LIGHT_FULL);
        }
      }
      delay(10);
    }
    while (digitalRead(WIFI_RESET_PIN) == LOW) delay(10);
    delay(40);
    ledChar->off();
    ledFinish->off();
    press->engage();
    logger->log("SERVOTEACH: LIMP phase done -- servo re-engaged");
  }

  // Phase 0. A sweep cannot be used to seat the horn: the shaft has to be
  // stationary and stay stationary for as long as the job takes. Nothing here
  // is on a timer, so the servo holds this angle until the button says
  // otherwise. Mid-range is deliberate -- seating the horn at 90 leaves equal
  // travel either side, so whichever way the press needs to go there is room
  // for it.
  const int fitHold = 90;
  press->hold(fitHold);
  logger->log(String("SERVOTEACH: FIT phase -- holding ") + fitHold +
              " deg, dead still. Seat the horn so the press sits "
              "roughly mid-travel toward the wheel, then tap.");
  {
    bool lit = false;
    for (int t = 0; digitalRead(WIFI_RESET_PIN) == HIGH; t++) {
      if (t % 50 == 0) {
        lit = !lit;
        if (lit) {
          ledChar->on(LIGHT_FULL);
          ledFinish->on(LIGHT_FULL);
        } else {
          ledChar->off();
          ledFinish->off();
        }
      }
      delay(10);
    }
    while (digitalRead(WIFI_RESET_PIN) == LOW) delay(10);
    delay(40);
    ledChar->off();
    ledFinish->off();
    logger->log("SERVOTEACH: FIT phase done -- starting sweeps");
  }

  for (int round = 0; round < 2; round++) {
    ledChar->on(LIGHT_FULL);
    if (round == 1) ledFinish->on(LIGHT_FULL);
    logger->log(String("SERVOTEACH: round ") + (round + 1) +
                "/2 -- stop the sweep where the press is " +
                (round == 0 ? "FULLY CLEAR of the wheel (rest)"
                            : "JUST TOUCHING the wheel (stamp)") +
                ". Tap the button to freeze.");

    int angle = sweepHi, dir = -1;
    while (captured[round] < 0) {
      press->hold(angle);
      if (angle % 10 == 0) {
        logger->log(String("SERVOTEACH: sweeping, angle=") + angle);
      }

      bool tapped = false;
      for (int t = 0; t < 9 && !tapped; t++) {
        int b = digitalRead(WIFI_RESET_PIN);
        if (b != lastBtn) {
          logger->log(String("SERVOTEACH: GPIO13 -> ") + (b ? "HIGH" : "LOW") +
                      " at angle " + angle);
          lastBtn = b;
        }
        if (b == LOW)
          tapped = true;
        else
          delay(10);
      }

      if (tapped) {
        while (digitalRead(WIFI_RESET_PIN) == LOW) delay(10);
        delay(40);
        lastBtn = HIGH;
        logger->log(String("SERVOTEACH: frozen at ") + angle +
                    " deg -- tap again within 6s to reject and resume");
        ledFinish->on(LIGHT_FULL);
        bool rejected = false;
        for (int t = 0; t < 600 && !rejected; t++) {
          if (digitalRead(WIFI_RESET_PIN) == LOW)
            rejected = true;
          else
            delay(10);
        }
        if (rejected) {
          while (digitalRead(WIFI_RESET_PIN) == LOW) delay(10);
          delay(40);
          logger->log("SERVOTEACH: rejected -- resuming sweep");
          if (round == 0) ledFinish->off();
        } else {
          captured[round] = angle;
          logger->log(String("SERVOTEACH: *** CAPTURED ") +
                      (round == 0 ? "REST" : "STAMP") + " = " + angle +
                      " deg ***");
        }
      }

      angle += dir;
      if (angle <= sweepLo) {
        angle = sweepLo;
        dir = 1;
      }
      if (angle >= sweepHi) {
        angle = sweepHi;
        dir = -1;
      }
    }
    delay(900);
  }

  ledChar->off();
  ledFinish->off();
  logger->log(String("SERVOTEACH: RESULT rest=") + captured[0] +
              " stamp=" + captured[1]);
  logger->log(String("SERVOTEACH: SET  REST_ANGLE ") + captured[0] +
              "   STAMP_ANGLE " + captured[1] + "  (both in Machine.h)");
  // Parked at the rest angle just taught, which is where the label maker will
  // hold it once Machine.h says so.
  press->hold(captured[0]);
  delay(500);
}

// The rig is over once setup() returns. The servo goes on holding the rest
// angle it was left at.
void loop() { delay(1000); }
