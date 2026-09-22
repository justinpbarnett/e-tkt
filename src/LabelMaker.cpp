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

#include "ArduinoDrivers.h"
#include "BenchRigs.h"
#include "Characters.h"
#include "Configuration.h"
#include "DaisyWheel.h"
#include "Display.h"
#include "ETKT.h"
#include "Feeder.h"
#include "HallSwitch.h"
#include "Light.h"
#include "Logger.h"
#include "Network.h"
#include "Press.h"
#include "Settings.h"
#include "Sound.h"
#include "Utility.h"

// ---------------------------------------------------------------------------
// The composition root.
//
// This is the one file that names ESP32Servo, AccelStepper and U8G2. Every
// module below takes its driver as a constructor parameter and talks to it
// through the small interfaces in Drivers.h, so replacing a driver -- a
// different servo library, a bigger screen, a recording fake on a development
// machine -- is an edit here and nowhere else. The host tests under test/ take
// the same three modules and hand them fakes instead.
//
// Order matters: a driver has to exist before the module that takes it, and
// these are file-scope initialisers, so they run top to bottom.
// ---------------------------------------------------------------------------

ArduinoServo* pressServo = new ArduinoServo();
ArduinoStepper* charStepper = new ArduinoStepper(
    AccelStepper::DRIVER, PIN_STEPPER_CHAR_STEP, PIN_STEPPER_CHAR_DIR);
// MICROSTEPS_FEED is 8, which is AccelStepper's HALF4WIRE -- an interface
// kind, not a microstep count, despite the name. The four numbers after it are
// the feeder's coil pins, carried over verbatim from Feeder's old constructor.
ArduinoStepper* feedStepper = new ArduinoStepper(MICROSTEPS_FEED, 15, 2, 16, 4);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C* screen =
    new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);

Logger* logger = new Logger();
Characters* characters = new Characters();
Sound* sound = new Sound(characters);
Display* display = new Display(sound, characters, screen);
Settings* settings = new Settings(logger);
Light* ledFinish = new Light(FINISH_LED_PIN);
Light* ledChar = new Light(CHARACTER_LED_PIN);
Press* press = new Press(logger, SERVO_PIN, ledChar, pressServo);
HallSwitch* hall = new HallSwitch(logger, HALL_PIN);
DaisyWheel* daisywheel =
    new DaisyWheel(logger, hall, characters, settings, charStepper);
Feeder* feeder = new Feeder(logger, feedStepper);
BenchRigs* benchRigs =
    new BenchRigs(logger, sound, press, feeder, ledChar, ledFinish);
ETKT* etkt = new ETKT(logger, settings, characters, display, daisywheel, hall,
                      feeder, press, sound, ledFinish, ledChar, benchRigs);
Network* network = new Network(logger, display, etkt, WIFI_RESET_PIN);

void setup() {
  // Initialize hardware
  etkt->initialize();

  // Play the splash screen
  display->playSplashScreen();

  // Start WiFi, network.
  network->initialize();

  // Display the ready "idle" screen.
  display->renderIdle();
}

void loop() { etkt->loop(); }
