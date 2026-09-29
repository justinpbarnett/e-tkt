// Bench rig: turn the daisy wheel slowly and find the magnet precisely.
//
//   pio run -e bench-hall -t upload && pio device monitor
//
// Sampling on a fixed stride is too coarse to tell a narrow trigger arc from a
// wheel that slipped, so this samples every step and logs only the edges. Two
// full revolutions must produce two triggers exactly one revolution apart; any
// other spacing means the shroud is slipping on the shaft or steps are lost.
//
// The sensor is read through HallSwitch, the way DaisyWheel reads it, so an
// edge here is an edge homing would see, INVERT_HALL_SENSOR_LOGIC included.
// The lowest and highest raw readings are logged too, and
// HALL_SENSOR_THRESHOLD in Machine.h belongs between them.
//
// The press comes up first and rests clear of the wheel, as it did when this
// rig ran inside the label maker's boot. Flash serial-upload afterwards to put
// the label maker back.

#include <Arduino.h>

#include "ArduinoDrivers.h"
#include "Configuration.h"
#include "HallSwitch.h"
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
HallSwitch* hall = new HallSwitch(logger, HALL_PIN);

void setup() {
  logger->initialize();
  ledChar->initialize();
  hall->initialize();
  press->initialize();

  pinMode(PIN_STEPPER_CHAR_STEP, OUTPUT);
  pinMode(PIN_STEPPER_CHAR_DIR, OUTPUT);
  pinMode(PIN_STEPPER_CHAR_ENABLE, OUTPUT);
  digitalWrite(PIN_STEPPER_CHAR_ENABLE, LOW);  // A4988 ENABLE is active low
  digitalWrite(PIN_STEPPER_CHAR_DIR, LOW);

  const int stepsPerRev = CHAR_STEP_COUNT * CHAR_MICROSTEPS;
  const int total = stepsPerRev * 2;
  logger->log("HALLSWEEP: 2 slow revolutions, ~20s. WATCH THE WHEEL.");
  int minRaw = 4095, maxRaw = 0, onAt = -1, hits = 0, firstOn = -1, lastOn = -1;
  bool wasOn = false;
  for (int s = 0; s < total; s++) {
    digitalWrite(PIN_STEPPER_CHAR_STEP, HIGH);
    delayMicroseconds(1500);
    digitalWrite(PIN_STEPPER_CHAR_STEP, LOW);
    delayMicroseconds(1400);
    const int raw = hall->reading();
    if (raw < minRaw) minRaw = raw;
    if (raw > maxRaw) maxRaw = raw;
    const bool isOn = hall->triggered();
    if (isOn && !wasOn) {
      onAt = s;
    } else if (!isOn && wasOn) {
      hits++;
      if (firstOn < 0) firstOn = onAt;
      lastOn = onAt;
      logger->log(String("HALLSWEEP: MAGNET steps ") + onAt + "-" + s +
                  " (arc " + (s - onAt) + " steps)");
    }
    wasOn = isOn;
    if (s % 800 == 0) {
      logger->log(String("HALLSWEEP: ") + s + "/" + total + " raw=" + raw);
    }
  }
  logger->log(String("HALLSWEEP: done. triggers=") + hits + " min=" + minRaw +
              " max=" + maxRaw + " threshold=" + HALL_SENSOR_THRESHOLD +
              (INVERT_HALL_SENSOR_LOGIC ? " (inverted)" : ""));
  if (hits >= 2) {
    int spacing = lastOn - firstOn;
    logger->log(
        String("HALLSWEEP: spacing=") + spacing + " expected=" + stepsPerRev +
        " error=" + (spacing - stepsPerRev) + " steps" +
        (abs(spacing - stepsPerRev) <= 16 ? "  OK, wheel tracks the shaft"
                                          : "  <== SLIPPING or losing steps"));
  } else {
    logger->log(
        "HALLSWEEP: fewer than 2 triggers -- either the wheel "
        "did not complete 2 revolutions (slipping) or the "
        "magnet never came back round.");
  }

  digitalWrite(PIN_STEPPER_CHAR_ENABLE, HIGH);  // release the motor
}

// The rig is over once setup() returns.
void loop() { delay(1000); }
