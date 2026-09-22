#include "BenchRigs.h"

#include <Arduino.h>

#include "Configuration.h"
#include "Press.h"

BenchRigs::BenchRigs(Logger* logger, Sound* sound, Press* press, Feeder* feeder,
                     Light* ledChar, Light* ledFinish) {
  this->logger = logger;
  this->sound = sound;
  this->press = press;
  this->feeder = feeder;
  this->ledChar = ledChar;
  this->ledFinish = ledFinish;
}

void BenchRigs::beforePeripherals() {
#if BENCH_SELFTEST
  // TEMPORARY bench self-test (2026-09-18) -- remove before assembly.
  // Runs before daisywheel->initialize(), which blocks in home() until the hall
  // triggers, so the 3.3 V peripherals can be checked with no motors attached.
  this->logger->log(
      "SELFTEST: character LED (GPIO 17) -- 3 blinks [already verified]");
  for (int i = 0; i < 3; i++) {
    this->ledChar->on(1.0f);
    delay(250);
    this->ledChar->off();
    delay(250);
  }

  this->logger->log("SELFTEST: finish LED (GPIO 5) -- 3 blinks");
  for (int i = 0; i < 3; i++) {
    this->ledFinish->on(1.0f);
    delay(250);
    this->ledFinish->off();
    delay(250);
  }

  this->logger->log("SELFTEST: both LEDs together -- 2 blinks");
  for (int i = 0; i < 2; i++) {
    this->ledChar->on(1.0f);
    this->ledFinish->on(1.0f);
    delay(500);
    this->ledChar->off();
    this->ledFinish->off();
    delay(500);
  }

  this->logger->log("SELFTEST: buzzer (GPIO 26) -- 2 beeps");
  this->sound->play(1000, 300);
  delay(500);
  this->sound->play(2000, 300);
  delay(500);

  this->logger->log(
      "SELFTEST: WiFi-reset button (GPIO 13) -- WAITING FOR HIGH");
  this->logger->log("SELFTEST: boot is paused here until GPIO13 reads HIGH.");
  this->logger->log(
      "SELFTEST: take your time -- no deadline, no credential wipe.");
  this->logger->log(
      "SELFTEST: character LED now MIRRORS GPIO13 -- lit = HIGH = good.");
  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
  {
    int last = digitalRead(WIFI_RESET_PIN);
    this->logger->log(
        String("SELFTEST: state now = ") +
        (last ? "HIGH (good, moving on)" : "LOW (shorted -- fix it)"));
    // Block until the pin is genuinely released. Ten minutes is a bench
    // backstop, not a real timeout; normally this exits in seconds.
    int highRun = 0;
    for (int i = 0; i < 12000 && highRun < 6; i++) {
      int now = digitalRead(WIFI_RESET_PIN);
      // Live indicator: character LED mirrors GPIO13 so the pin can be probed
      // at the bench without a serial capture. LED lit = HIGH = good.
      if (now) {
        this->ledChar->on(1.0f);
      } else {
        this->ledChar->off();
      }
      highRun = now ? highRun + 1 : 0;
      if (now != last) {
        this->logger->log(String("SELFTEST: t=") + (i / 20) + "s  -> " +
                          (now ? "HIGH" : "LOW"));
        last = now;
      }
      if (i % 100 == 0 && i > 0) {
        this->logger->log(String("SELFTEST: t=") + (i / 20) +
                          "s  waiting, state = " + (now ? "HIGH" : "LOW"));
      }
      delay(50);
    }
    if (highRun >= 6) {
      this->logger->log(
          "SELFTEST: GPIO13 released -- now press the button a few times");
      int presses = 0;
      int prev = HIGH;
      for (int i = 0; i < 1200; i++) {
        int now = digitalRead(WIFI_RESET_PIN);
        if (now) {
          this->ledChar->on(1.0f);
        } else {
          this->ledChar->off();
        }
        if (now != prev) {
          if (now == LOW) {
            presses++;
            this->ledFinish->on(1.0f);
            this->logger->log(String("SELFTEST: PRESS #") + presses);
          } else {
            this->ledFinish->off();
            this->logger->log("SELFTEST: release");
          }
          prev = now;
        }
        delay(50);
      }
      this->logger->log(String("SELFTEST: presses detected = ") + presses);
    } else {
      this->logger->log("SELFTEST: gave up waiting -- GPIO13 never went HIGH");
    }
    if (digitalRead(WIFI_RESET_PIN) == LOW) {
      this->logger->log("SELFTEST: ends LOW -- credentials WILL be wiped");
    } else {
      this->logger->log("SELFTEST: ends HIGH -- credentials safe");
    }
  }

  this->logger->log("SELFTEST: done");
#endif
}

void BenchRigs::beforeHoming() {
#if BENCH_FEEDER_TEST
  // TEMPORARY bench test for the ULN2003 + 28BYJ-48 feeder.
  // Deliberately placed after feeder->initialize() and before
  // daisywheel->initialize(), which blocks in home() until the hall triggers.
  this->logger->log("FEEDTEST: 6 rounds, each 8 feeds = one full revolution");
  for (int round = 1; round <= 6; round++) {
    this->logger->log(String("FEEDTEST: round ") + round + "/6 starting");
    for (int i = 1; i <= 8; i++) {
      this->feeder->feed(1);
      delay(300);
    }
    this->feeder->deenergize();
    this->logger->log(String("FEEDTEST: round ") + round + "/6 done");
    delay(2000);
  }
  this->logger->log("FEEDTEST: all rounds done");
#endif

#if BENCH_A4988_TEST
  // TEMPORARY bench test for the A4988 + NEMA 17. Drives STEP/DIR/ENABLE
  // directly rather than through DaisyWheel, whose initialize() calls home().
  // Staged deliberately: a hold phase to prove the driver delivers current at
  // all, then one revolution at each of three step rates with no acceleration
  // ramp. A motor that turns slowly but not quickly is stalling on the daisy
  // wheel's inertia, not starved of current or missing its step signal.
  pinMode(PIN_STEPPER_CHAR_STEP, OUTPUT);
  pinMode(PIN_STEPPER_CHAR_DIR, OUTPUT);
  pinMode(PIN_STEPPER_CHAR_ENABLE, OUTPUT);
  digitalWrite(PIN_STEPPER_CHAR_ENABLE, LOW);  // A4988 ENABLE is active low
  {
    const int stepsPerRev = CHAR_STEP_COUNT * CHAR_MICROSTEPS;  // 200 * 16
    digitalWrite(PIN_STEPPER_CHAR_DIR, LOW);

    this->logger->log(
        "A4988TEST: HOLD 15s -- TRY TO TURN THE WHEEL BY HAND NOW");
    delay(15000);
    this->logger->log("A4988TEST: hold over");

    const int halfPeriods[] = {2000, 1000, 500};
    for (int p = 0; p < 3; p++) {
      int hp = halfPeriods[p];
      this->logger->log(String("A4988TEST: 1 rev at ") + (500000 / hp) +
                        " steps/s");
      for (int s = 0; s < stepsPerRev; s++) {
        digitalWrite(PIN_STEPPER_CHAR_STEP, HIGH);
        delayMicroseconds(hp);
        digitalWrite(PIN_STEPPER_CHAR_STEP, LOW);
        delayMicroseconds(hp);
      }
      delay(2000);
    }
  }
  digitalWrite(PIN_STEPPER_CHAR_ENABLE, HIGH);  // release the motor
  this->logger->log(
      "A4988TEST: done -- released, wheel should now spin freely");
#endif

#if BENCH_SERVO_TEST
  // Teach the two press angles at the bench and log them.
  //
  // A hobby servo has no position feedback, so the lever cannot be posed by
  // hand and read back. Inverted instead: the firmware sweeps and the tact
  // switch on WIFI_RESET_PIN stops it.
  //
  // Deliberately silent. ESP32Servo and ESP32Tone both allocate LEDC channels,
  // and a tone() that lands on the channel attach() took would strip the servo
  // of its 50 Hz signal for the rest of the run -- which is indistinguishable
  // at the bench from "the servo is dead". Rounds are signalled on the LEDs
  // instead: character LED steady = round 1 (rest), both LEDs = round 2
  // (stamp).
  //
  // The angle is logged every 10 degrees so the sweep can be confirmed from
  // the capture even when nothing appears to move on the bench, and every
  // GPIO13 transition is logged so a button that is never seen is obvious.
  {
    pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
    const int sweepHi = 180, sweepLo = 0;
    int captured[2] = {-1, -1};
    int lastBtn = digitalRead(WIFI_RESET_PIN);
    this->logger->log(
        String("SERVOTEACH: GPIO13 at start = ") +
        (lastBtn ? "HIGH (released, good)" : "LOW (stuck/shorted)"));

    // Phase 0. A sweep cannot be used to seat the horn: the shaft has to be
    // stationary and stay stationary for as long as the job takes. Nothing
    // here is on a timer, so the servo holds this angle until the button says
    // otherwise. Mid-range is deliberate -- seating the horn at 90 leaves
    // equal travel either side, so whichever way the press needs to go there
    // is room for it.
    // Phase L. The horn cannot be posed by hand while the servo is driving it
    // -- forcing a live MG996R is how its nylon gears strip -- so cut the
    // pulses first and let it go slack. Nothing is measured here, because
    // nothing can be: this phase exists so the geometry can be chosen by feel
    // before the horn is committed to a spline.
    this->press->release();
    this->logger->log(
        "SERVOTEACH: LIMP phase -- servo released, push the press "
        "by hand and decide where it should sit. Tap when decided.");
    {
      bool flip = false;
      for (int t = 0; digitalRead(WIFI_RESET_PIN) == HIGH; t++) {
        if (t % 40 == 0) {
          flip = !flip;
          if (flip) {
            this->ledChar->on(1.0f);
            this->ledFinish->off();
          } else {
            this->ledChar->off();
            this->ledFinish->on(1.0f);
          }
        }
        delay(10);
      }
      while (digitalRead(WIFI_RESET_PIN) == LOW) delay(10);
      delay(40);
      this->ledChar->off();
      this->ledFinish->off();
      this->press->engage();
      this->logger->log("SERVOTEACH: LIMP phase done -- servo re-engaged");
    }

    const int fitHold = 90;
    this->press->hold(fitHold);
    this->logger->log(String("SERVOTEACH: FIT phase -- holding ") + fitHold +
                      " deg, dead still. Seat the horn so the press sits "
                      "roughly mid-travel toward the wheel, then tap.");
    {
      bool lit = false;
      for (int t = 0; digitalRead(WIFI_RESET_PIN) == HIGH; t++) {
        if (t % 50 == 0) {
          lit = !lit;
          if (lit) {
            this->ledChar->on(1.0f);
            this->ledFinish->on(1.0f);
          } else {
            this->ledChar->off();
            this->ledFinish->off();
          }
        }
        delay(10);
      }
      while (digitalRead(WIFI_RESET_PIN) == LOW) delay(10);
      delay(40);
      this->ledChar->off();
      this->ledFinish->off();
      this->logger->log("SERVOTEACH: FIT phase done -- starting sweeps");
    }

    for (int round = 0; round < 2; round++) {
      this->ledChar->on(1.0f);
      if (round == 1) this->ledFinish->on(1.0f);
      this->logger->log(String("SERVOTEACH: round ") + (round + 1) +
                        "/2 -- stop the sweep where the press is " +
                        (round == 0 ? "FULLY CLEAR of the wheel (rest)"
                                    : "JUST TOUCHING the wheel (stamp)") +
                        ". Tap the button to freeze.");

      int angle = sweepHi, dir = -1;
      while (captured[round] < 0) {
        this->press->hold(angle);
        if (angle % 10 == 0) {
          this->logger->log(String("SERVOTEACH: sweeping, angle=") + angle);
        }

        bool tapped = false;
        for (int t = 0; t < 9 && !tapped; t++) {
          int b = digitalRead(WIFI_RESET_PIN);
          if (b != lastBtn) {
            this->logger->log(String("SERVOTEACH: GPIO13 -> ") +
                              (b ? "HIGH" : "LOW") + " at angle " + angle);
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
          this->logger->log(String("SERVOTEACH: frozen at ") + angle +
                            " deg -- tap again within 6s to reject and resume");
          this->ledFinish->on(1.0f);
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
            this->logger->log("SERVOTEACH: rejected -- resuming sweep");
            if (round == 0) this->ledFinish->off();
          } else {
            captured[round] = angle;
            this->logger->log(String("SERVOTEACH: *** CAPTURED ") +
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

    this->ledChar->off();
    this->ledFinish->off();
    this->logger->log(String("SERVOTEACH: RESULT rest=") + captured[0] +
                      " stamp=" + captured[1]);
    this->logger->log(String("SERVOTEACH: SET  REST_ANGLE ") + captured[0] +
                      "   STAMP_ANGLE " + captured[1] +
                      "  (both in Machine.h)");
    this->press->hold(captured[0]);
    delay(500);
  }
#endif
#if BENCH_HALL_MONITOR
  // Rotate the daisy wheel slowly and find the magnet precisely. Sampling on a
  // fixed stride is too coarse to tell a narrow trigger arc from a wheel that
  // slipped, so instead sample every step and log only the edges. Two full
  // revolutions must produce two triggers exactly stepsPerRevolution apart; any
  // other spacing means the shroud is slipping on the shaft or steps are lost.
  pinMode(PIN_STEPPER_CHAR_STEP, OUTPUT);
  pinMode(PIN_STEPPER_CHAR_DIR, OUTPUT);
  pinMode(PIN_STEPPER_CHAR_ENABLE, OUTPUT);
  digitalWrite(PIN_STEPPER_CHAR_ENABLE, LOW);  // A4988 ENABLE is active low
  digitalWrite(PIN_STEPPER_CHAR_DIR, LOW);
  {
    const int stepsPerRev = CHAR_STEP_COUNT * CHAR_MICROSTEPS;
    const int total = stepsPerRev * 2;
    this->logger->log("HALLSWEEP: 2 slow revolutions, ~20s. WATCH THE WHEEL.");
    int minRaw = 4095, onAt = -1, hits = 0, firstOn = -1, lastOn = -1;
    bool wasOn = false;
    for (int s = 0; s < total; s++) {
      digitalWrite(PIN_STEPPER_CHAR_STEP, HIGH);
      delayMicroseconds(1500);
      digitalWrite(PIN_STEPPER_CHAR_STEP, LOW);
      delayMicroseconds(1400);
      int raw = analogRead(HALL_PIN);
      if (raw < minRaw) minRaw = raw;
      bool isOn = raw < HALL_SENSOR_THRESHOLD;
      if (isOn && !wasOn) {
        onAt = s;
      } else if (!isOn && wasOn) {
        hits++;
        if (firstOn < 0) firstOn = onAt;
        lastOn = onAt;
        this->logger->log(String("HALLSWEEP: MAGNET steps ") + onAt + "-" + s +
                          " (arc " + (s - onAt) + " steps)");
      }
      wasOn = isOn;
      if (s % 800 == 0) {
        this->logger->log(String("HALLSWEEP: ") + s + "/" + total +
                          " raw=" + raw);
      }
    }
    this->logger->log(String("HALLSWEEP: done. triggers=") + hits +
                      " min=" + minRaw + " threshold=" + HALL_SENSOR_THRESHOLD);
    if (hits >= 2) {
      int spacing = lastOn - firstOn;
      this->logger->log(String("HALLSWEEP: spacing=") + spacing +
                        " expected=" + stepsPerRev +
                        " error=" + (spacing - stepsPerRev) + " steps" +
                        (abs(spacing - stepsPerRev) <= 16
                             ? "  OK, wheel tracks the shaft"
                             : "  <== SLIPPING or losing steps"));
    } else {
      this->logger->log(
          "HALLSWEEP: fewer than 2 triggers -- either the wheel "
          "did not complete 2 revolutions (slipping) or the "
          "magnet never came back round.");
    }
  }
  digitalWrite(PIN_STEPPER_CHAR_ENABLE, HIGH);  // release the motor
#endif
}
