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
// IMPORTANT: do not forget to upload the files in "data" folder using SPIFFS
//
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

#include <Arduino.h>
#include <U8g2lib.h>

#include "Api.h"
#include "ArduinoDrivers.h"
#include "Configuration.h"
#include "DaisyWheel.h"
#include "ETKT.h"
#include "Feeder.h"
#include "HallSwitch.h"
#include "Light.h"
#include "Logger.h"
#include "Network.h"
#include "OledDisplay.h"
#include "Press.h"
#include "Printhead.h"
#include "Roll.h"
#include "Settings.h"
#include "Sound.h"
#include "StopSignal.h"
#include "Utility.h"

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
// The one place that knows the screen is an OLED. ETKT and Network take it
// as a Display, which is all either of them draws through.
OledDisplay* display = new OledDisplay(sound, screen);
Settings* settings = new Settings(logger);
Roll* roll = new Roll(logger);
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
ETKT* etkt = new ETKT(logger, settings, display, printhead, feeder, roll, sound,
                      ledFinish, ledChar, stopSignal);
// Everything the device answers under /api/, which the webserver hands every
// such request to.
Api* api = new Api(etkt, logger);
Network* network = new Network(logger, display, api, WIFI_RESET_PIN);

void setup() {
  // Initialize hardware
  etkt->initialize();

  // Play the splash screen
  display->playSplashScreen();

  // Start WiFi, network.
  network->initialize();

  // Display the ready "idle" screen.
  display->renderIdle(false);
}

void loop() { etkt->loop(); }
