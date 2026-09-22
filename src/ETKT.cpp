#include "ETKT.h"

#include <Arduino.h>
#include <FreeRTOS.h>

#include <map>
#include <mutex>
#include <thread>

#include "Characters.h"
#include "Configuration.h"
#include "DaisyWheel.h"
#include "Display.h"
#include "Feeder.h"
#include "HallSwitch.h"
#include "Light.h"
#include "Logger.h"
#include "Press.h"
#include "Progress.h"
#include "Settings.h"
#include "Sound.h"

ETKT::ETKT(Logger* logger, Settings* settings, Characters* characters,
           Display* display, DaisyWheel* daisywheel, HallSwitch* hall,
           Feeder* feeder, Press* press, Sound* sound, Light* ledFinish,
           Light* ledChar, BenchRigs* benchRigs) {
  // Upstream never assigned this one, and initialize() dereferences it on its
  // first line. It only ever worked because Logger holds no state, so the
  // uninitialised pointer was never actually read through.
  this->logger = logger;
  this->settings = settings;
  this->display = display;
  this->daisywheel = daisywheel;
  this->hall = hall;
  this->feeder = feeder;
  this->press = press;
  this->sound = sound;
  this->ledFinish = ledFinish;
  this->ledChar = ledChar;
  this->characters = characters;
  this->benchRigs = benchRigs;

  this->command = NULL;
  this->progress = 0;
  this->lock = new std::mutex();
  this->eventGroup = xEventGroupCreate();
}

ETKT::~ETKT() {
  // Empty for now
}

void ETKT::initialize() {
  this->logger->initialize();
  this->characters->initialize();
  this->ledFinish->initialize();
  this->ledChar->initialize();
  this->sound->initialize();

  this->benchRigs->beforePeripherals();
  this->settings->initialize();
  this->display->initialize();
  this->hall->initialize();
  this->press->initialize();
  this->feeder->initialize();

  this->benchRigs->beforeHoming();

  this->daisywheel->initialize();
}

StatusUpdate* ETKT::createStatus() {
  auto status = new StatusUpdate();
  this->lock->lock();
  if (this->command != NULL) {
    status->currentCommand = this->command->command;
    status->currentCommandString = this->command->commandAsString();
    status->currentLabel = this->command->label;
    status->progress = this->progress;
  }
  this->lock->unlock();
  status->align = this->settings->getAlignFactor();
  status->force = this->settings->getForceFactor();
  return status;
}

void ETKT::cutCommand() {
  auto command = new CommandOptions();
  command->command = Command::CUT;
  this->startCommand(command);
}

void ETKT::feedCommand() {
  auto command = new CommandOptions();
  command->command = Command::FEED;
  this->startCommand(command);
}

void ETKT::reelCommand() {
  auto command = new CommandOptions();
  command->command = Command::REEL;
  this->startCommand(command);
}

void ETKT::testAlignCommand(int align) {
  auto command = new CommandOptions();
  command->command = Command::TEST_ALIGN;
  command->align = align;
  this->startCommand(command);
}

void ETKT::testFullCommand(int align, int force) {
  auto command = new CommandOptions();
  command->command = Command::TEST_FULL;
  command->align = align;
  command->force = force;
  this->startCommand(command);
}

void ETKT::saveCommand(int align, int force) {
  auto command = new CommandOptions();
  command->command = Command::SAVE;
  command->align = align;
  command->force = force;
  this->startCommand(command);
}

void ETKT::homeCommand() {
  auto command = new CommandOptions();
  command->command = Command::HOME;
  this->startCommand(command);
}

void ETKT::moveCommand(String character) {
  auto command = new CommandOptions();
  command->command = Command::MOVE;
  command->label = character;
  this->startCommand(command);
}

void ETKT::tagCommand(String label) {
  auto command = new CommandOptions();
  command->command = Command::TAG;
  command->label = label;
  this->startCommand(command);
}

void ETKT::cut(int force) {
  if (!ENABLE_CUT) {
    delay(500);
    return;
  }
  // moves to a specific char (*) then presses label three times (more
  // vigorously)
  this->daisywheel->move("*", this->settings->getAlignFactor());
  // force 0 means "caller did not say", i.e. use whatever is saved. The full
  // test button passes the force being trialled instead, so the cut is made at
  // the same setting as the characters it just stamped.
  const int cutForce =
      force > 0 ? force : (int)this->settings->getForceFactor();
  for (int i = 0; i < 3; i++) {
    this->press->press(true, cutForce, false);
  }
}

void ETKT::startCommand(CommandOptions* command) {
  this->lock->lock();
  if (this->command != NULL) {
    this->lock->unlock();
    delete command;
    throw PrinterBusyException();
  }
  this->command = command;
  xEventGroupSetBits(this->eventGroup, BIT0);
  this->lock->unlock();
}

void ETKT::loop() {
  // Wait for the signal, with a timeout of 500 ms just in case.
  xEventGroupWaitBits(eventGroup, BIT0, pdTRUE, pdFALSE,
                      500 / portTICK_PERIOD_MS);

  // check for a command
  this->lock->lock();
  if (this->command == NULL) {
    this->lock->unlock();
    return;
  }
  this->lock->unlock();

  // Do the task
  switch (this->command->command) {
    case Command::CUT:
      this->cutCommandInternal();
      break;
    case Command::FEED:
      this->feedCommandInternal();
      break;
    case Command::REEL:
      this->reelCommandInternal();
      break;
    case Command::TEST_ALIGN:
      this->testCommandInternal();
      break;
    case Command::TEST_FULL:
      this->testCommandFullInternal();
      break;
    case Command::SAVE:
      this->saveCommandInternal();
      break;
    case Command::TAG:
      this->tagCommandInternal();
      break;
    case Command::HOME:
      this->homeCommandInternal();
      break;
    case Command::MOVE:
      this->moveCommandInternal();
      break;
    case Command::IDLE:
      break;
  }

  // Park the machine before the command slot is released. Everything below
  // talks to hardware -- a full OLED redraw with a QR code on it, then three
  // motors -- and it used to run with the lock held, which stalled every
  // status poll from the web task for as long as that took. Parking first
  // also closes a gap: startCommand() goes on refusing new work until the
  // slot is genuinely clear, so nothing can begin against a machine that is
  // still being put away.
  this->display->renderIdle();
  this->daisywheel->deenergize();
  this->press->rest();
  this->feeder->deenergize();

  this->lock->lock();
  delete this->command;
  this->command = NULL;
  this->progress = 0;
  this->lock->unlock();
}

void ETKT::feedCommandInternal() {
  this->display->render(Screen::FEEDING);
  this->ledFinish->on(1.0f / 8);
  this->press->rest();
  delay(500);
  this->feeder->feed();
  this->ledFinish->off();
  this->ledChar->off();
}

void ETKT::reelCommandInternal() {
  this->display->render(Screen::REELING);
  this->ledFinish->on(1.0f / 8);
  this->press->rest();
  delay(500);

  this->feeder->feed(16);
  this->ledFinish->off();
  this->ledChar->off();
}

void ETKT::cutCommandInternal() {
  this->display->render(Screen::CUTTING);
  this->ledChar->on(0.2f);
  this->press->rest();
  delay(500);

  this->cut();
  ledChar->off();
}

void ETKT::saveCommandInternal() {
  this->logger->log("saving settings");

  display->initialize();
  display->renderSaved(this->command->align, this->command->force);
  // The waits used to live inside the two renderers. They are the caller's
  // business: how long a confirmation stays up is a decision about this
  // command, not about how to draw a screen.
  delay(SAVED_SCREEN_MS);
  ledFinish->off();

  settings->save(this->command->align, this->command->force);
  display->render(Screen::REBOOTING);
  delay(REBOOT_SCREEN_MS);
  ledFinish->off();
  ledChar->off();
  delay(500);

  // TODO: Does the device actually need to reboot?
  // I think all the state gets updated properly without it.
  ESP.restart();
}

void ETKT::testCommandInternal() {
  // No align or force on this screen. renderTest() took both and drew
  // neither, and showing them would be worse than showing nothing: the press
  // below deliberately ignores command->force and uses the minimum.
  display->render(Screen::TESTING);
  ledFinish->off();

  this->daisywheel->move("M", this->command->align);
  // Deliberately the minimum force, matching docs/diy/calibration.md: this
  // button "will slowly and lightly press the daisy wheel letter" to check
  // that the press lands centred on the character. Force is calibrated
  // separately, with the full test button against real tape.
  //
  // It must stay at minimum force. This is the only path that passes
  // slow=true, so it is the only press that holds at peak for
  // PRESS_TEST_DWELL_MS rather than PRESS_DWELL_MS -- and the calibration doc
  // sends the user here while the force field is wound up to 9 ("take the
  // opportunity to see if the alignment is correct"). A full-bite peak held
  // for seconds is a stalled servo, which is what wears an MG996R's gears and
  // reams the P_press splines.
  this->press->press(false, CALIBRATION_VALUE_MIN, true);
}

void ETKT::testCommandFullInternal() {
  String label = "E-TKT";
  this->feeder->feed();
  for (int i = 0; i < label.length(); i++) {
    auto character = label.substring(i, i + 1);
    this->feeder->feed();
    this->daisywheel->move(character, this->command->align);
    this->press->press(false, this->command->force, false);
  }
  this->feeder->feed();
  this->daisywheel->move("*", this->command->align);
  this->cut(this->command->force);
}

void ETKT::homeCommandInternal() {
  this->ledFinish->on(1.0f);
  this->ledChar->on(1.0f);
  this->press->rest();
  delay(500);
  this->daisywheel->home(this->settings->getAlignFactor());
  delay(1000);
}

void ETKT::moveCommandInternal() {
  this->press->rest();
  delay(500);
  this->daisywheel->move(this->command->label, this->settings->getAlignFactor());
}

void ETKT::tagCommandInternal() {
  auto label = this->command->label;
  label.toUpperCase();
  // enables servo
  this->press->rest();
  delay(500);

  this->logger->log(String("print ") + label);

  // all possible characters: $-.23456789*abcdefghijklmnopqrstuvwxyz♡☆♪€@
  int labelLength = Utility::utf8Length(label);

  this->ledChar->on(0.2f);

  this->display->renderProgress(0, label);

  if (label == " TASCHENRECHNER " || label == " POCKET CALCULATOR " ||
      label == " DENTAKU " || label == " CALCULADORA " ||
      label == " MINI CALCULATEUR ") {
    this->sound->playMelody(
        "*4599845887*459984588764599845887*4599845887",
        "88843888484888438884848884388848488843888484");  // ♪ I'm the operator
                                                          // with my pocket
                                                          // calculator ♪
  } else {
    this->sound->playLabel(label);
  }

  // home daisy wheel
  this->daisywheel->home(this->settings->getAlignFactor());

  this->feeder->feed();

  for (int i = 0; i < labelLength; i++) {
    auto character = Utility::utf8CharAt(label, i);
    if (character != " ") {
      this->daisywheel->move(character, this->settings->getAlignFactor());
      this->press->press(false, this->settings->getForceFactor(), false);
    }

    this->feeder->feed();
    delay(500);

    this->display->renderProgress(i + 1, label);

    this->lock->lock();
    this->progress = progressPercent(i + 1, labelLength);
    this->lock->unlock();
  }

  if (labelLength < 6 &&
      labelLength !=
          1)  // minimum label length to make sure the user can grab it
  {
    int spaceDelta = 6 - labelLength;
    for (int i = 0; i < spaceDelta; i++) {
      this->feeder->feed();
    }
  }

  this->cut();

  this->ledChar->off();
  display->render(Screen::FINISHED);

  this->logger->log("Blinking LED");
  // Blink the finish led a few times.
  for (int i = 0; i < 5; i++) {
    this->ledFinish->off();
    delay(100);
    this->ledFinish->on(0.5f);
    delay(100);
  }

  this->logger->log("Fading LED");
  // Then fade it out.
  for (int i = 128; i >= 0; i--) {
    this->ledFinish->on(i / 128.0f);
    delay(25);
  }
  this->ledFinish->off();
  this->logger->log("Printing Complete");
}
