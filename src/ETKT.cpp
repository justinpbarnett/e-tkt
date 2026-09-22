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

// The one statement of what commands this device has. Columns, in order:
// the enumerator, the name it answers to on the wire and in /api/<name>,
// whether it reads align, force and label, and the handler loop() runs.
//
// Before this table the same nine commands were written out four times over
// -- a name switch, a factory method each, a dispatch switch with no default
// case, and the route list in Network.cpp -- and home and move had already
// fallen out of the webapp's copy.
const CommandSpec ETKT::COMMANDS[] = {
    //            name         align  force  label field  handler
    {Command::CUT, "cut", false, false, NULL, &ETKT::cutCommandInternal},
    {Command::FEED, "feed", false, false, NULL, &ETKT::feedCommandInternal},
    {Command::REEL, "reel", false, false, NULL, &ETKT::reelCommandInternal},
    // Align only. This test presses at the minimum force by design -- see
    // testCommandInternal -- so a force in the body is ignored, not refused,
    // which keeps a stale cached script.js working.
    {Command::TEST_ALIGN, "testalign", true, false, NULL,
     &ETKT::testCommandInternal},
    {Command::TEST_FULL, "testfull", true, true, NULL,
     &ETKT::testCommandFullInternal},
    {Command::SAVE, "save", true, true, NULL, &ETKT::saveCommandInternal},
    {Command::TAG, "tag", false, false, "tag", &ETKT::tagCommandInternal},
    {Command::HOME, "home", false, false, NULL, &ETKT::homeCommandInternal},
    {Command::MOVE, "move", false, false, "character",
     &ETKT::moveCommandInternal},
    // IDLE is a status, not a job: no handler, and no route is registered for
    // it. It keeps a name because /api/status reports one.
    {Command::IDLE, "idle", false, false, NULL, NULL},
};

const size_t ETKT::COMMAND_COUNT =
    sizeof(ETKT::COMMANDS) / sizeof(ETKT::COMMANDS[0]);

// IDLE is the last enumerator, so its value is the index of the last row and
// the table has to be one longer. Add an enumerator without a row and the
// build stops here rather than the device answering "unknown" at runtime.
static_assert(sizeof(ETKT::COMMANDS) / sizeof(ETKT::COMMANDS[0]) ==
                  static_cast<size_t>(Command::IDLE) + 1,
              "every Command needs a row in ETKT::COMMANDS");

const CommandSpec* commandSpec(Command command) {
  for (size_t i = 0; i < ETKT::COMMAND_COUNT; i++) {
    if (ETKT::COMMANDS[i].command == command) {
      return &ETKT::COMMANDS[i];
    }
  }
  return NULL;
}

const CommandSpec* commandSpecByName(const String& name) {
  for (size_t i = 0; i < ETKT::COMMAND_COUNT; i++) {
    if (name == ETKT::COMMANDS[i].name) {
      return &ETKT::COMMANDS[i];
    }
  }
  return NULL;
}

const char* commandName(Command command) {
  const CommandSpec* spec = commandSpec(command);
  return spec == NULL ? "unknown" : spec->name;
}

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

void ETKT::submit(const CommandOptions& options) {
  // Copied on the way in. The webserver builds its options on the request
  // task's stack and the device needs them to outlive the request, but who
  // owns what should not be part of the interface: a refused command leaves
  // the caller's copy exactly as it found it.
  CommandOptions* queued = new CommandOptions(options);

  this->lock->lock();
  if (this->command != NULL) {
    this->lock->unlock();
    delete queued;
    throw PrinterBusyException();
  }
  this->command = queued;
  xEventGroupSetBits(this->eventGroup, BIT0);
  this->lock->unlock();
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

  // Do the task. The command's row says which handler to run. A command with
  // no row at all is a bug worth hearing about; a row with no handler is
  // IDLE, which is a status rather than a job, so it falls straight through
  // to the parking code below.
  const CommandSpec* spec = commandSpec(this->command->command);
  if (spec == NULL) {
    this->logger->log(String("No table row for command ") +
                      (int)this->command->command);
  } else if (spec->run != NULL) {
    (this->*(spec->run))();
  }

  // Park the machine before the command slot is released. Everything below
  // talks to hardware -- a full OLED redraw with a QR code on it, then three
  // motors -- and it used to run with the lock held, which stalled every
  // status poll from the web task for as long as that took. Parking first
  // also closes a gap: submit() goes on refusing new work until the
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

  // What a label may say is CHARACTERS in CharacterSet.h, less the cut
  // mark, plus the space. printableCharacters() is that list; the webapp
  // fetches it rather than keeping one of its own.
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
