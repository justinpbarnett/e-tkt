#pragma once

// The whole label maker on the host, module for module, built the way
// LabelMaker.cpp builds it on the board. Where it meets the hardware it has
// fakes instead: FakeServo and FakeStepper for the three motors, FakeDisplay
// for the OLED, a FakeMagnet on the wheel's hub, and the stubs for the core,
// the sounder and the EEPROM.
//
// For the tests that drive the machine from outside, through the job runner
// or the Api, and so need every module rather than one, and for the
// simulator in src/simulator, which runs one behind the Api. Build one after
// stubReset(), which clears the hooks it installs. By the time the
// constructor returns the machine has booted: the wheel has found home, and
// the screen's record of what booting drew is cleared.

#include <vector>

#include "Arduino.h"
#include "DaisyWheel.h"
#include "ETKT.h"
#include "FakeDisplay.h"
#include "FakeDrivers.h"
#include "FakeMagnet.h"
#include "Feeder.h"
#include "HallSwitch.h"
#include "Light.h"
#include "Logger.h"
#include "Press.h"
#include "Printhead.h"
#include "Roll.h"
#include "Settings.h"
#include "Sound.h"
#include "StopSignal.h"
#include "StrokeLog.h"

class HostMachine {
 public:
  // In the order they are built. Each one takes only the ones above it.
  FakeServo pressServo;
  FakeStepper charStepper;
  FakeStepper feedStepper;
  FakeDisplay display;
  FakeMagnet magnet;
  StrokeLog strokes;

  Logger logger;
  StopSignal stopSignal;
  Sound sound;
  Settings settings;
  Roll roll;
  Light ledFinish;
  Light ledChar;
  Press press;
  HallSwitch hall;
  DaisyWheel daisywheel;
  Printhead printhead;
  Feeder feeder;
  ETKT etkt;

  HostMachine()
      // Anywhere but where the shaft starts, so the boot home has to find it.
      : magnet(&charStepper, 1000),
        strokes(&pressServo, &magnet),
        sound(&stopSignal),
        settings(&logger),
        roll(&logger),
        ledFinish(FINISH_LED_PIN, &stopSignal),
        ledChar(CHARACTER_LED_PIN, &stopSignal),
        press(&logger, SERVO_PIN, &ledChar, &pressServo),
        hall(&logger, HALL_PIN),
        daisywheel(&logger, &hall, &charStepper, &stopSignal),
        printhead(&logger, &daisywheel, &press, &stopSignal),
        feeder(&logger, &feedStepper, &stopSignal),
        etkt(&logger, &settings, &display, &printhead, &feeder, &roll, &sound,
             &ledFinish, &ledChar, &stopSignal) {
    this->magnet.install();
    this->etkt.initialize();
    this->display.clear();
  }

  /**
   * @brief Drops what the fakes have recorded so far.
   *
   * They record for tests, which read the recording afterwards. The
   * simulator runs its machine for longer than any test and reads none of
   * it, and a day of labels would record millions of steps. The last stroke
   * stays, since StrokeLog adds to it while the press is down, and the servo
   * stays attached, which FakeServo::clear() would undo.
   */
  void forget() {
    this->pressServo.calls.clear();
    this->charStepper.clear();
    this->feedStepper.clear();
    this->display.clear();
    std::vector<Stroke>& made = this->strokes.strokes;
    if (made.size() > 1) {
      made.erase(made.begin(), made.end() - 1);
    }
  }
};
