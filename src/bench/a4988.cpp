// Bench rig: the A4988 and the NEMA 17 that turn the daisy wheel.
//
// Drives STEP, DIR and ENABLE by hand rather than through DaisyWheel, whose
// initialize() homes and so needs the hall sensor working first. Staged on
// purpose: a hold phase to prove the driver delivers current at all, then one
// revolution at each of three step rates with no acceleration ramp. A motor
// that turns slowly but not quickly is stalling on the daisy wheel's inertia,
// not starved of current or missing its step signal. Verified on machine 1,
// 2026-09-21.
//
//   pio run -e bench-a4988 -t upload && pio device monitor
//
// The press comes up first and rests clear of the wheel, as it did when this
// rig ran inside the label maker's boot. Flash serial-upload afterwards to put
// the label maker back.

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
Press* press = new Press(logger, SERVO_PIN, ledChar, pressServo);

void setup() {
  logger->initialize();
  ledChar->initialize();
  press->initialize();

  pinMode(PIN_STEPPER_CHAR_STEP, OUTPUT);
  pinMode(PIN_STEPPER_CHAR_DIR, OUTPUT);
  pinMode(PIN_STEPPER_CHAR_ENABLE, OUTPUT);
  digitalWrite(PIN_STEPPER_CHAR_ENABLE, LOW);  // A4988 ENABLE is active low

  const int stepsPerRev = CHAR_STEP_COUNT * CHAR_MICROSTEPS;  // 200 * 16
  digitalWrite(PIN_STEPPER_CHAR_DIR, LOW);

  logger->log("A4988TEST: HOLD 15s -- TRY TO TURN THE WHEEL BY HAND NOW");
  delay(15000);
  logger->log("A4988TEST: hold over");

  const int halfPeriods[] = {2000, 1000, 500};
  for (int p = 0; p < 3; p++) {
    int hp = halfPeriods[p];
    logger->log(String("A4988TEST: 1 rev at ") + (500000 / hp) + " steps/s");
    for (int s = 0; s < stepsPerRev; s++) {
      digitalWrite(PIN_STEPPER_CHAR_STEP, HIGH);
      delayMicroseconds(hp);
      digitalWrite(PIN_STEPPER_CHAR_STEP, LOW);
      delayMicroseconds(hp);
    }
    delay(2000);
  }

  digitalWrite(PIN_STEPPER_CHAR_ENABLE, HIGH);  // release the motor
  logger->log("A4988TEST: done -- released, wheel should now spin freely");
}

// The rig is over once setup() returns.
void loop() { delay(1000); }
