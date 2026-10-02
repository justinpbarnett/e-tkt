// MIT License

// Copyright (c) 2022 Andrei Speridião

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//
// for more information, please visit https://github.com/andreisperid/E-TKT
//

// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
//
// The files in the "data" folder are part of this firmware. A build puts
// them in, so there is nothing to upload beside it, with SPIFFS or otherwise.
//
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

#include <Arduino.h>
#include <U8g2lib.h>

#include "Api.h"
#include "ArduinoDrivers.h"
#include "Button.h"
#include "Configuration.h"
#include "DaisyWheel.h"
#include "ETKT.h"
#include "Esp32Radio.h"
#include "Feeder.h"
#include "HallSwitch.h"
#include "LastRun.h"
#include "Light.h"
#include "LinkSupervisor.h"
#include "Logger.h"
#include "Network.h"
#include "NetworkSettings.h"
#include "OledDisplay.h"
#include "Press.h"
#include "Printhead.h"
#include "Roll.h"
#include "Settings.h"
#include "Sound.h"
#include "StopSignal.h"
#include "Utility.h"
#include "esp_heap_caps.h"

// ---------------------------------------------------------------------------
// The composition root.
//
// This is the one file that builds the ESP32Servo, AccelStepper and U8G2
// drivers. Every module below takes its driver as a constructor parameter and
// talks to it through a small interface, the motors through Drivers.h and the
// screen through Display.h. So replacing a driver -- a different servo
// library, a bigger screen, a recording fake on a development machine -- is an
// edit here and nowhere else. The host tests under test/ build the same
// modules, the job runner among them, and hand them fakes instead.
//
// Order matters: a driver has to exist before the module that takes it, and
// these are file-scope initialisers, so they run top to bottom.
// ---------------------------------------------------------------------------

ArduinoServo* pressServo = new ArduinoServo();
ArduinoStepper* charStepper = new ArduinoStepper(
    AccelStepper::DRIVER, PIN_STEPPER_CHAR_STEP, PIN_STEPPER_CHAR_DIR);
// MICROSTEPS_FEED is 8, which is AccelStepper's HALF4WIRE -- an interface
// kind, not a microstep count, despite the name.
ArduinoStepper* feedStepper = new ArduinoStepper(
    MICROSTEPS_FEED, PIN_STEPPER_FEED_COIL_1, PIN_STEPPER_FEED_COIL_2,
    PIN_STEPPER_FEED_COIL_3, PIN_STEPPER_FEED_COIL_4);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C* screen =
    new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);

Logger* logger = new Logger();
// Raised by ETKT::stop() from the webserver's task, and obeyed by everything
// that moves, sounds or blinks for a job, so they all share the one.
StopSignal* stopSignal = new StopSignal();
Sound* sound = new Sound(stopSignal);
// The one place that knows the screen is an OLED. ETKT, the link and Network
// take it as a Display, which is all any of them draws through.
OledDisplay* display = new OledDisplay(sound, screen);
Settings* settings = new Settings(logger);
Roll* roll = new Roll(logger);
LastRun* lastRun = new LastRun(logger);
Light* ledFinish = new Light(FINISH_LED_PIN, stopSignal);
Light* ledChar = new Light(CHARACTER_LED_PIN, stopSignal);
Press* press = new Press(logger, SERVO_PIN, ledChar, pressServo);
HallSwitch* hall = new HallSwitch(logger, HALL_PIN);
// Built before the wheel and the printhead: the tape feeds while the wheel
// turns, and the press waits for it to stop.
Feeder* feeder = new Feeder(logger, feedStepper, stopSignal);
DaisyWheel* daisywheel =
    new DaisyWheel(logger, hall, charStepper, stopSignal, feeder);
Printhead* printhead =
    new Printhead(logger, daisywheel, press, stopSignal, feeder);
ETKT* etkt = new ETKT(logger, settings, display, printhead, feeder, roll,
                      lastRun, sound, ledFinish, ledChar, stopSignal);
// How the machine is reached. The link decides: which network it joins, and
// when it opens its own. It works the radio through the Radio interface, as
// the host tests work a fake one, so this is the one place that knows the
// radio is the chip's.
NetworkSettings* networkSettings = new NetworkSettings(logger);
Esp32Radio* radio = new Esp32Radio(logger);
LinkSupervisor* linkSupervisor =
    new LinkSupervisor(logger, radio, networkSettings, display);
// Everything the device answers under /api/, which the webserver hands every
// such request to: what the job runner does, and how the machine is reached.
Api* api = new Api(etkt, linkSupervisor, networkSettings, logger);
Network* network =
    new Network(logger, display, api, radio, networkSettings, linkSupervisor);
// The pin is still named for what the switch on it was first for. Held
// through the start, it still is that.
Button* button = new Button(WIFI_RESET_PIN, etkt, logger);

// Reads the button for as long as the machine is on. A task of its own, so
// that a press stops a job while loop() below is busy running it.
static void buttonTask(void*) {
  for (;;) {
    button->poll();
    vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
  }
}

void setup() {
  // Initialize hardware
  etkt->initialize();

  // Play the splash screen
  display->playSplashScreen();

  // The button, held from the splash on, is how the machine is made to
  // forget its networks. It restarts to do so, and nothing below runs.
  button->initialize();
  if (button->heldAtStart()) {
    network->forgetNetworks();
  }

  // Start the webserver and the link. Neither waits for a network, so the
  // machine is ready, and its button works, whether or not it is on one.
  network->initialize();

  // Display the ready "idle" screen, or the notice that the roll is out.
  etkt->showIdle();

  // Core 0, beside the link's task and for its reason: core 1 is the
  // motors'. A press logs, and the log lines allocate, so the stack is a
  // step past the 4 KB a task that only reads a pin would get by on.
  xTaskCreatePinnedToCore(buttonTask, "button", 6144, NULL, 1, NULL, 0);

  // What is left once everything above is built. A connection is made of
  // this memory, so it is how many of them the machine has room for. These
  // are the numbers the status gives; ESP.getFreeHeap() counts memory that
  // nothing can be given as well, and reads as more than there is.
  logger->log(String("memory free: ") +
              heap_caps_get_free_size(MALLOC_CAP_8BIT) +
              " bytes, largest block " +
              heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void loop() { etkt->loop(); }
