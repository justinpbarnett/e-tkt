// Bench rig: the 3.3 V parts.
//
// Looks for the screen on I2C and draws on it, blinks both LEDs, sounds the
// buzzer, then waits on the wifi-reset button and counts presses of it. The
// screen names each step as it runs, so the rig can be followed without a
// serial capture. Nothing here drives a motor, so it runs on a board with
// motors wired and no 12 V, and nothing is able to move. The LEDs, buzzer and
// button were verified on machine 1, 2026-09-18; the screen step was added
// for machine 2's bring-up, 2026-10-09.
//
//   pio run -e bench-selftest -t upload && pio device monitor
//
// A rig is a firmware of its own, with its own setup() and loop() and none of
// the label maker's. Flash serial-upload afterwards to put the label maker
// back.

#include <Arduino.h>
#include <Wire.h>

#include "BenchScreen.h"
#include "Configuration.h"
#include "Light.h"
#include "Logger.h"
#include "Sound.h"
#include "StopSignal.h"

Logger* logger = new Logger();
// Nothing raises it here. Sound and Light take one because a job can stop
// them.
StopSignal* stopSignal = new StopSignal();
Sound* sound = new Sound(stopSignal);
Light* ledChar = new Light(CHARACTER_LED_PIN, stopSignal);
Light* ledFinish = new Light(FINISH_LED_PIN, stopSignal);
BenchScreen* screen = new BenchScreen();

// The address an SSD1306 answers to with its address pin low, as on every
// module this machine has used.
const uint8_t SCREEN_ADDRESS = 0x3C;

// Asks every I2C address in turn and logs the ones that answer. Returns
// whether the screen's address did.
bool findScreen() {
  Wire.begin(SDA, SCL);
  bool found = false;
  int answers = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      logger->log(String("SELFTEST: I2C answer at 0x") + String(address, HEX));
      answers++;
      found = found || address == SCREEN_ADDRESS;
    }
  }
  logger->log(String("SELFTEST: I2C addresses answering = ") + answers);
  return found;
}

void setup() {
  logger->initialize();
  ledFinish->initialize();
  ledChar->initialize();
  sound->initialize();

  logger->log(String("SELFTEST: screen (I2C SDA ") + SDA + ", SCL " + SCL +
              ") -- looking for it at 0x" + String(SCREEN_ADDRESS, HEX));
  if (findScreen()) {
    logger->log("SELFTEST: screen answers -- drawing on it");
    screen->begin();
    logger->log("SELFTEST: screen -- 3 s all lit: look for dark pixels");
    screen->fill();
    delay(3000);
    logger->log(
        "SELFTEST: screen -- 5 s frame: all 4 edges, no noise down a side");
    screen->show("E-TKT SELFTEST", "frame on 4 edges");
    delay(5000);
  } else {
    logger->log(
        "SELFTEST: screen does NOT answer -- check its 4 wires and 3.3 V");
  }

  logger->log("SELFTEST: character LED (GPIO 17) -- 3 blinks");
  screen->show("CHARACTER LED", "GPIO 17: 3 blinks");
  ledChar->blink(3, LIGHT_FULL, 250, 250);
  ledChar->off();

  logger->log("SELFTEST: finish LED (GPIO 5) -- 3 blinks");
  screen->show("FINISH LED", "GPIO 5: 3 blinks");
  ledFinish->blink(3, LIGHT_FULL, 250, 250);
  ledFinish->off();

  logger->log("SELFTEST: both LEDs together -- 2 blinks");
  screen->show("BOTH LEDS", "together: 2 blinks");
  // The one shape Light::blink cannot express: two LEDs lit and darkened
  // together. Blinking them one after the other would test the same two pins
  // twice over rather than testing that they can both be driven at once,
  // which is the point of this third pass.
  for (int i = 0; i < 2; i++) {
    ledChar->on(LIGHT_FULL);
    ledFinish->on(LIGHT_FULL);
    delay(500);
    ledChar->off();
    ledFinish->off();
    delay(500);
  }

  logger->log("SELFTEST: buzzer (GPIO 26) -- 2 beeps");
  screen->show("BUZZER", "GPIO 26: 2 beeps");
  sound->play(1000, 300);
  delay(500);
  sound->play(2000, 300);
  delay(500);

  logger->log("SELFTEST: WiFi-reset button (GPIO 13) -- WAITING FOR HIGH");
  screen->show("BUTTON GPIO 13", "waiting for release");
  logger->log("SELFTEST: the rig is paused here until GPIO13 reads HIGH.");
  logger->log("SELFTEST: take your time -- no deadline, no credential wipe.");
  logger->log(
      "SELFTEST: character LED now MIRRORS GPIO13 -- lit = HIGH = good.");
  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
  // The pin reads low in the moment its pull-up turns on (see Button.cpp),
  // which machine 2 showed here as a short that was not there.
  delay(20);
  int last = digitalRead(WIFI_RESET_PIN);
  logger->log(String("SELFTEST: state now = ") +
              (last ? "HIGH (good, moving on)" : "LOW (shorted -- fix it)"));
  // Block until the pin is genuinely released. Ten minutes is a bench
  // backstop, not a real timeout; normally this exits in seconds.
  int highRun = 0;
  for (int i = 0; i < 12000 && highRun < 6; i++) {
    int now = digitalRead(WIFI_RESET_PIN);
    // Live indicator: character LED mirrors GPIO13 so the pin can be probed
    // at the bench without a serial capture. LED lit = HIGH = good.
    if (now) {
      ledChar->on(LIGHT_FULL);
    } else {
      ledChar->off();
    }
    highRun = now ? highRun + 1 : 0;
    if (now != last) {
      logger->log(String("SELFTEST: t=") + (i / 20) + "s  -> " +
                  (now ? "HIGH" : "LOW"));
      last = now;
    }
    if (i % 100 == 0 && i > 0) {
      logger->log(String("SELFTEST: t=") + (i / 20) +
                  "s  waiting, state = " + (now ? "HIGH" : "LOW"));
    }
    delay(50);
  }
  if (highRun >= 6) {
    logger->log(
        "SELFTEST: GPIO13 released -- now press the button a few times");
    screen->show("BUTTON GPIO 13", "press it: 0 so far");
    int presses = 0;
    int prev = HIGH;
    for (int i = 0; i < 1200; i++) {
      int now = digitalRead(WIFI_RESET_PIN);
      if (now) {
        ledChar->on(LIGHT_FULL);
      } else {
        ledChar->off();
      }
      if (now != prev) {
        if (now == LOW) {
          presses++;
          ledFinish->on(LIGHT_FULL);
          logger->log(String("SELFTEST: PRESS #") + presses);
          screen->show("BUTTON GPIO 13",
                       String("press it: ") + presses + " so far");
        } else {
          ledFinish->off();
          logger->log("SELFTEST: release");
        }
        prev = now;
      }
      delay(50);
    }
    logger->log(String("SELFTEST: presses detected = ") + presses);
  } else {
    logger->log("SELFTEST: gave up waiting -- GPIO13 never went HIGH");
  }
  // The label maker wipes its wifi credentials when it boots with the button
  // held, so this is what the next boot of the label maker would do.
  if (digitalRead(WIFI_RESET_PIN) == LOW) {
    logger->log("SELFTEST: ends LOW -- credentials WILL be wiped");
  } else {
    logger->log("SELFTEST: ends HIGH -- credentials safe");
  }

  ledChar->off();
  ledFinish->off();
  screen->show("SELFTEST DONE", "see the serial log");
  logger->log("SELFTEST: done");
}

// The rig is over once setup() returns.
void loop() { delay(1000); }
