// Bench rig: the 3.3 V parts.
//
// Blinks both LEDs, sounds the buzzer, then waits on the wifi-reset button
// and counts presses of it. Nothing here drives a motor, so it runs on a board
// with no motor wired and nothing able to move. Verified on machine 1,
// 2026-09-18.
//
//   pio run -e bench-selftest -t upload && pio device monitor
//
// A rig is a firmware of its own, with its own setup() and loop() and none of
// the label maker's. Flash serial-upload afterwards to put the label maker
// back.

#include <Arduino.h>

#include "Characters.h"
#include "Configuration.h"
#include "Light.h"
#include "Logger.h"
#include "Sound.h"
#include "StopSignal.h"

Logger* logger = new Logger();
Characters* characters = new Characters();
// Nothing raises it here. Sound and Light take one because a job can stop
// them.
StopSignal* stopSignal = new StopSignal();
Sound* sound = new Sound(characters, stopSignal);
Light* ledChar = new Light(CHARACTER_LED_PIN, stopSignal);
Light* ledFinish = new Light(FINISH_LED_PIN, stopSignal);

void setup() {
  logger->initialize();
  characters->initialize();
  ledFinish->initialize();
  ledChar->initialize();
  sound->initialize();

  logger->log(
      "SELFTEST: character LED (GPIO 17) -- 3 blinks [already verified]");
  ledChar->blink(3, LIGHT_FULL, 250, 250);
  ledChar->off();

  logger->log("SELFTEST: finish LED (GPIO 5) -- 3 blinks");
  ledFinish->blink(3, LIGHT_FULL, 250, 250);
  ledFinish->off();

  logger->log("SELFTEST: both LEDs together -- 2 blinks");
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
  sound->play(1000, 300);
  delay(500);
  sound->play(2000, 300);
  delay(500);

  logger->log("SELFTEST: WiFi-reset button (GPIO 13) -- WAITING FOR HIGH");
  logger->log("SELFTEST: the rig is paused here until GPIO13 reads HIGH.");
  logger->log("SELFTEST: take your time -- no deadline, no credential wipe.");
  logger->log(
      "SELFTEST: character LED now MIRRORS GPIO13 -- lit = HIGH = good.");
  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
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
  logger->log("SELFTEST: done");
}

// The rig is over once setup() returns.
void loop() { delay(1000); }
