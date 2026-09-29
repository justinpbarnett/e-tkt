// Bench rig: the ULN2003 and the 28BYJ-48 that feed the tape.
//
// Six rounds of eight feeds, which is one full turn of the feed motor each,
// with a pause between rounds to see where the cog stopped. Verified on
// machine 1, 2026-09-21.
//
//   pio run -e bench-feeder -t upload && pio device monitor
//
// The press comes up first and rests clear of the tape, as it did when this
// rig ran inside the label maker's boot. The feeds are not charged to the
// roll, because the label maker counts only its own: load a roll once the
// label maker is back on the board. Flash serial-upload to put it back.

#include "Feeder.h"

#include <Arduino.h>

#include "ArduinoDrivers.h"
#include "Configuration.h"
#include "Light.h"
#include "Logger.h"
#include "Press.h"
#include "StopSignal.h"

ArduinoServo* pressServo = new ArduinoServo();
// MICROSTEPS_FEED is 8, which is AccelStepper's HALF4WIRE -- an interface
// kind, not a microstep count, despite the name.
ArduinoStepper* feedStepper = new ArduinoStepper(
    MICROSTEPS_FEED, PIN_STEPPER_FEED_COIL_1, PIN_STEPPER_FEED_COIL_2,
    PIN_STEPPER_FEED_COIL_3, PIN_STEPPER_FEED_COIL_4);

Logger* logger = new Logger();
// Nothing raises it here. The modules take one because a job can stop them.
StopSignal* stopSignal = new StopSignal();
Light* ledChar = new Light(CHARACTER_LED_PIN, stopSignal);
Press* press = new Press(logger, SERVO_PIN, ledChar, pressServo);
Feeder* feeder = new Feeder(logger, feedStepper, stopSignal);

void setup() {
  logger->initialize();
  ledChar->initialize();
  press->initialize();
  feeder->initialize();

  logger->log("FEEDTEST: 6 rounds, each 8 feeds = one full revolution");
  for (int round = 1; round <= 6; round++) {
    logger->log(String("FEEDTEST: round ") + round + "/6 starting");
    for (int i = 1; i <= 8; i++) {
      feeder->feed(1);
      delay(300);
    }
    feeder->deenergize();
    logger->log(String("FEEDTEST: round ") + round + "/6 done");
    delay(2000);
  }
  logger->log("FEEDTEST: all rounds done");
}

// The rig is over once setup() returns.
void loop() { delay(1000); }
