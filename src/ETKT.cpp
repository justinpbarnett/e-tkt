#include "ETKT.h"

#include <Arduino.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "CharacterSet.h"
#include "Configuration.h"
#include "DaisyWheel.h"
#include "Display.h"
#include "Feeder.h"
#include "HallSwitch.h"
#include "Light.h"
#include "Logger.h"
#include "Press.h"
#include "Progress.h"
#include "Roll.h"
#include "Settings.h"
#include "Sound.h"
#include "StopSignal.h"
#include "Tape.h"
#include "Utility.h"

// How the finish LED celebrates a finished label: five half-brightness
// flashes, then a slow fade to dark. Timings, not shapes -- Light owns what a
// blink and a fade are.
static const int FINISH_BLINK_TIMES = 5;
static const int FINISH_BLINK_MS = 100;
static const int FINISH_FADE_MS = 3225;

// The one statement of what commands this device has. Columns, in order:
// the enumerator, the name it answers to on the wire and in /api/<name>,
// whether it reads align, force and label, whether it may be given a number
// of copies and a roll length, and the handler loop() runs.
//
// Before this table the same nine commands were written out four times over
// -- a name switch, a factory method each, a dispatch switch with no default
// case, and the route list in Network.cpp -- and home and move had already
// fallen out of the webapp's copy.
const CommandSpec ETKT::COMMANDS[] = {
    // name  align  force  label field  label  copies  roll  handler
    {Command::CUT, "cut", false, false, NULL, false, false, false,
     &ETKT::cutCommandInternal},
    {Command::FEED, "feed", false, false, NULL, false, false, false,
     &ETKT::feedCommandInternal},
    // A reel is a new roll going in, so it is where the roll's length is
    // declared.
    {Command::REEL, "reel", false, false, NULL, false, false, true,
     &ETKT::reelCommandInternal},
    // Align only. This test presses at the minimum force by design -- see
    // testCommandInternal -- so a force in the body is ignored, not refused,
    // which keeps a stale cached script.js working.
    {Command::TEST_ALIGN, "testalign", true, false, NULL, false, false, false,
     &ETKT::testCommandInternal},
    {Command::TEST_FULL, "testfull", true, true, NULL, false, false, false,
     &ETKT::testCommandFullInternal},
    {Command::SAVE, "save", true, true, NULL, false, false, false,
     &ETKT::saveCommandInternal},
    {Command::TAG, "tag", false, false, "tag", true, true, false,
     &ETKT::tagCommandInternal},
    {Command::HOME, "home", false, false, NULL, false, false, false,
     &ETKT::homeCommandInternal},
    {Command::MOVE, "move", false, false, "character", false, false, false,
     &ETKT::moveCommandInternal},
    // IDLE is a status, not a job: no handler, and no route is registered for
    // it. It keeps a name because /api/status reports one.
    {Command::IDLE, "idle", false, false, NULL, false, false, false, NULL},
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
  // The rows are written in enum order, so the enum is the index -- no
  // search. Counting the rows (see the static_assert above) cannot catch a
  // duplicated enumerator paired with a missing one, because the total
  // still matches; a search would then hand back the wrong handler for one
  // command and nothing for the other. Asking the row to agree that it is
  // the row for this command turns that into the NULL the caller already
  // logs.
  const size_t index = static_cast<size_t>(command);
  if (index >= ETKT::COMMAND_COUNT) {
    return NULL;
  }
  const CommandSpec* spec = &ETKT::COMMANDS[index];
  return spec->command == command ? spec : NULL;
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

ETKT::ETKT(Logger* logger, Settings* settings, Display* display,
           DaisyWheel* daisywheel, HallSwitch* hall, Feeder* feeder, Roll* roll,
           Press* press, Sound* sound, Light* ledFinish, Light* ledChar,
           StopSignal* stopSignal) {
  // Upstream never assigned this one, and initialize() dereferences it on its
  // first line. It only ever worked because Logger holds no state, so the
  // uninitialised pointer was never actually read through.
  this->logger = logger;
  this->settings = settings;
  this->display = display;
  this->daisywheel = daisywheel;
  this->hall = hall;
  this->feeder = feeder;
  this->roll = roll;
  this->press = press;
  this->sound = sound;
  this->ledFinish = ledFinish;
  this->ledChar = ledChar;
  this->stopSignal = stopSignal;

  this->command = NULL;
  this->progress = 0;
  this->copy = 0;
  this->printed = 0;
  this->stoppingAfterLabel = false;
  // Nothing has fed yet, so there is nothing to charge.
  this->accountedFeeds = 0;
  this->feedsAtLastCut = 0;
}

ETKT::~ETKT() {
  // Empty for now
}

void ETKT::initialize() {
  this->logger->initialize();
  this->ledFinish->initialize();
  this->ledChar->initialize();
  this->sound->initialize();
  this->settings->initialize();
  this->roll->initialize();
  this->display->initialize();
  this->hall->initialize();
  this->press->initialize();
  this->feeder->initialize();
  this->daisywheel->initialize();
}

StatusUpdate ETKT::createStatus() {
  StatusUpdate status;

  // Outside the lock on purpose. The lock guards the in-flight command and
  // its progress; settings are not behind it, and cannot change underneath
  // this anyway -- the only thing that writes them is the save command,
  // which reboots the device on its way out.
  status.align = this->settings->getAlignFactor();
  status.force = this->settings->getForceFactor();
  // Roll keeps a lock of its own. Taken outside this one so the two are never
  // held together, which is the easy way never to take them in two orders.
  status.roll = this->roll->state();

  this->lock.lock();
  if (this->command != NULL) {
    status.currentCommand = this->command->command;
    status.currentLabel = this->command->label;
    status.progress = this->progress;
    if (this->command->command == Command::TAG) {
      status.copy = this->copy;
      status.copies = this->command->copies;
    }
    // A stop now outranks a stop after the label, which it overtakes.
    if (this->stopSignal->raised()) {
      status.stop = PendingStop::NOW;
    } else if (this->stoppingAfterLabel) {
      status.stop = PendingStop::AFTER_LABEL;
    }
  }
  status.stopped = this->lastStopped;
  this->lock.unlock();

  return status;
}

void ETKT::submit(const CommandOptions& options) {
  // Copied on the way in. The webserver builds its options on the request
  // task's stack and the device needs them to outlive the request, but who
  // owns what should not be part of the interface: a refused command leaves
  // the caller's copy exactly as it found it.
  CommandOptions* queued = new CommandOptions(options);

  this->lock.lock();
  if (this->command != NULL) {
    this->lock.unlock();
    delete queued;
    throw PrinterBusyException();
  }
  this->command = queued;
  // A new job, so the last one's stop is old news.
  this->lastStopped = StoppedCommand();
  this->queued.notify_one();
  this->lock.unlock();
}

StopResult ETKT::stop() {
  this->lock.lock();
  if (this->command == NULL) {
    this->lock.unlock();
    return StopResult::IDLE;
  }
  if (this->command->command == Command::SAVE) {
    this->lock.unlock();
    return StopResult::UNSTOPPABLE;
  }
  // Under the lock, so the stop can only land on the command it was meant
  // for: loop() clears it under the same lock as it lets that command go.
  this->stopSignal->raise();
  this->lock.unlock();

  this->logger->log("Stopping now");
  return StopResult::STOPPING;
}

StopResult ETKT::stopAfterLabel() {
  this->lock.lock();
  if (this->command == NULL) {
    this->lock.unlock();
    return StopResult::IDLE;
  }
  if (this->command->command != Command::TAG) {
    this->lock.unlock();
    return StopResult::UNSTOPPABLE;
  }
  this->stoppingAfterLabel = true;
  this->lock.unlock();

  this->logger->log("Stopping after this label");
  return StopResult::STOPPING;
}

void ETKT::accountForTape() {
  const long fed = this->feeder->feeds();
  if (fed > this->accountedFeeds) {
    this->roll->use((uint32_t)(fed - this->accountedFeeds));
    this->accountedFeeds = fed;
  }
}

void ETKT::cut() { this->cutAt((int)this->settings->getForceFactor()); }

void ETKT::cutAt(int force) {
  if (!ENABLE_CUT) {
    delay(500);
    return;
  }
  // moves to a specific char (*) then presses label three times (more
  // vigorously)
  if (!this->daisywheel->move(CUT_CHARACTER,
                              this->settings->getAlignFactor())) {
    // move() cuts the coil current when it refuses, so the wheel is now both
    // unreferenced and free to turn. Pressing three times at full force into
    // whatever slot it stopped at would emboss a letter where the cut mark
    // belongs, and leave the tape uncut anyway. A stop is the other reason
    // move() says no, and that one was asked for.
    if (!this->stopSignal->raised()) {
      this->logger->warn("Skipped the cut: the wheel would not reach the mark");
    }
    return;
  }
  for (int i = 0; i < 3; i++) {
    if (this->stopSignal->shouldStop()) {
      return;
    }
    this->press->press(true, force, false);
  }
}

void ETKT::loop() {
  // Wait for a command, with a timeout of 500 ms just in case. The wait
  // checks the slot before it sleeps, so a command queued before loop() got
  // here is taken at once rather than half a second late.
  std::unique_lock<std::mutex> guard(this->lock);
  this->queued.wait_for(guard, std::chrono::milliseconds(500),
                        [this] { return this->command != NULL; });

  // Take a copy of which command it is while the lock is held. Only this
  // function ever clears the slot, so reading it again after the unlock
  // would in fact be safe today -- but that is a fact about the rest of the
  // class, not about this code, and the next writer to the slot would
  // silently break it. The enum is two bytes; copy it out.
  if (this->command == NULL) {
    return;
  }
  const Command running = this->command->command;
  guard.unlock();

  // Do the task. The command's row says which handler to run. A command with
  // no row at all is a bug worth hearing about; a row with no handler is
  // IDLE, which is a status rather than a job, so it falls straight through
  // to the parking code below.
  const CommandSpec* spec = commandSpec(running);
  if (spec == NULL) {
    this->logger->log(String("No table row for command ") + (int)running);
  } else if (spec->run != NULL) {
    (this->*(spec->run))();
  }

  // Four commands feed, and every feed is tape off the roll. Charged before
  // the slot is released, so the first idle status already shows it.
  this->accountForTape();

  // Whether a stop cut this command short, as against arriving while it was
  // finishing anyway.
  const bool stopped = this->stopSignal->cutShort();
  if (stopped) {
    this->logger->log(String("Stopped ") + commandName(running));
  }

  // Park the motors before the command slot is released, and outside the
  // lock, which used to be held through all of this and stalled every status
  // poll from the web task for as long as it took. Parking before the release
  // closes a gap: submit() goes on refusing new work until the slot is
  // genuinely clear, so nothing can begin against a machine that is still
  // being put away.
  this->daisywheel->deenergize();
  this->press->rest();
  this->feeder->deenergize();

  this->lock.lock();
  if (stopped) {
    StoppedCommand record;
    record.command = running;
    if (running == Command::TAG) {
      record.printed = this->printed;
      record.copies = this->command->copies;
    }
    if (running == Command::TAG || running == Command::TEST_FULL) {
      // Tape fed since the last cut is a label nothing has cut off.
      record.unfinished = this->feeder->feeds() > this->feedsAtLastCut;
    }
    this->lastStopped = record;
  }
  delete this->command;
  this->command = NULL;
  this->progress = 0;
  this->copy = 0;
  this->printed = 0;
  this->stoppingAfterLabel = false;
  // Down again before the slot opens, so no stop outlives its command.
  this->stopSignal->clear();
  this->lock.unlock();

  // The idle screen comes after the release, not before it. It is a full
  // redraw with a QR code on it, and a job posted while it draws is one a
  // parked machine can take. Only this task draws, so the next job's first
  // screen still waits for this one.
  this->display->renderIdle(stopped);
}

void ETKT::feedCommandInternal() {
  this->display->render(Screen::FEEDING);
  this->ledFinish->on(LIGHT_FAINT);
  this->press->rest();
  delay(500);
  this->feeder->feed();
  this->ledFinish->off();
  this->ledChar->off();
}

void ETKT::reelCommandInternal() {
  this->display->render(Screen::REELING);
  this->ledFinish->on(LIGHT_FAINT);
  this->press->rest();
  delay(500);

  // A reel is a new roll going in. Anything fed before this came off the old
  // one, so it is charged there before the count starts again. The feeds
  // below thread the new roll through to the cutter, which is tape off the
  // new roll, so they are charged to it when loop() next accounts.
  this->accountForTape();
  const int length = this->command->rollLengthMm > 0
                         ? this->command->rollLengthMm
                         : (int)this->roll->state().lengthMm;
  this->roll->load(length);

  this->feeder->feed(REEL_FEEDS);
  this->ledFinish->off();
  this->ledChar->off();
}

void ETKT::cutCommandInternal() {
  this->display->render(Screen::CUTTING);
  this->ledChar->on(LIGHT_DIM);
  this->press->rest();
  delay(500);

  this->cut();
  ledChar->off();
}

void ETKT::saveCommandInternal() {
  this->logger->log("saving settings");

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

  if (!this->daisywheel->move("M", this->command->align) ||
      this->stopSignal->shouldStop()) {
    return;
  }
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
  this->feedsAtLastCut = this->feeder->feeds();
  this->feeder->feed();
  for (int i = 0; i < label.length(); i++) {
    if (this->stopSignal->shouldStop()) {
      return;
    }
    auto character = label.substring(i, i + 1);
    this->feeder->feed();
    if (this->daisywheel->move(character, this->command->align) &&
        !this->stopSignal->shouldStop()) {
      this->press->press(false, this->command->force, false);
    }
  }
  if (this->stopSignal->shouldStop()) {
    return;
  }
  this->feeder->feed();
  this->cutAt(this->command->force);
}

void ETKT::homeCommandInternal() {
  this->ledFinish->on(LIGHT_FULL);
  this->ledChar->on(LIGHT_FULL);
  this->press->rest();
  delay(500);
  this->daisywheel->home(this->settings->getAlignFactor());
  delay(1000);
}

void ETKT::moveCommandInternal() {
  this->press->rest();
  delay(500);
  // Nothing presses after this one, so a refused move costs no tape -- but it
  // leaves the wheel parked somewhere other than the slot that was asked for,
  // and saying so is the difference between a stuck wheel and a quiet one.
  // Unless it was stopped, which is the operator's doing and no surprise.
  if (!this->daisywheel->move(this->command->label,
                              this->settings->getAlignFactor()) &&
      !this->stopSignal->raised()) {
    this->logger->warn(String("The wheel would not reach '") +
                       this->command->label + "'");
  }
}

void ETKT::tagCommandInternal() {
  auto label = this->command->label;
  label.toUpperCase();
  const int copies = this->command->copies;
  // On its first label from the start, not from its first feed. The panel
  // polls throughout, and "label 0 of 3" while the press settles is nothing
  // an operator can make sense of.
  this->lock.lock();
  this->copy = 1;
  this->lock.unlock();
  // enables servo
  this->press->rest();
  delay(500);

  if (copies > 1) {
    this->logger->log(String("print ") + label + " x " + copies);
  } else {
    this->logger->log(String("print ") + label);
  }

  // What a label may say is CHARACTERS in CharacterSet.h, less the cut
  // mark, plus the space. printableCharacters() is that list; the webapp
  // fetches it rather than keeping one of its own.
  int labelLength = Utility::utf8Length(label);

  this->ledChar->on(LIGHT_DIM);

  this->feedsAtLastCut = this->feeder->feeds();
  for (int copy = 1; copy <= copies; copy++) {
    this->lock.lock();
    this->copy = copy;
    this->progress = 0;
    this->lock.unlock();

    this->display->renderProgress(0, label, copy, copies);

    // Once a run, not once a label. The tune says printing has started, and
    // the same few seconds of it before every label of a long run would be
    // most of a minute of music for nothing.
    if (copy == 1) {
      this->playTune(label);
    }

    this->printLabel(label, labelLength, copy, copies);
    this->accountForTape();
    if (this->stopSignal->cutShort()) {
      // Stopped partway through this label, which is left on the tape.
      break;
    }
    this->feedsAtLastCut = this->feeder->feeds();
    this->lock.lock();
    this->printed = copy;
    this->lock.unlock();
    if (copy == copies) {
      break;
    }

    // Between one cut and the next feed, where both kinds of stop can end a
    // run without leaving anything on the tape. A stop now, obeyed here,
    // still counts as cutting the run short.
    if (this->stopSignal->shouldStop()) {
      break;
    }
    // Only ever read here. See stopAfterLabel().
    this->lock.lock();
    const bool afterThisLabel = this->stoppingAfterLabel;
    this->lock.unlock();
    if (afterThisLabel) {
      this->logger->log(String("Stopped after ") + copy + " of " + copies);
      break;
    }
  }

  this->ledChar->off();
  if (this->stopSignal->cutShort()) {
    return;
  }
  this->logger->log("Printing Complete");
  // A stop that came as the last label was being cut was too late to cut
  // anything short. The operator still asked for the machine to stop, so it
  // does, and skips the four seconds of celebration. One that comes during
  // them ends them; see Light.
  if (this->stopSignal->raised()) {
    return;
  }

  display->render(Screen::FINISHED);

  // Blink, then fade out. blink() leaves the LED lit at LIGHT_HALF and the
  // fade restarts from LIGHT_FULL, which is a jump; it is how this has always
  // looked, and at zero milliseconds apart it is not a thing anyone sees.
  this->ledFinish->blink(FINISH_BLINK_TIMES, LIGHT_HALF, FINISH_BLINK_MS,
                         FINISH_BLINK_MS);
  this->ledFinish->fadeOut(LIGHT_FULL, FINISH_FADE_MS);
}

void ETKT::playTune(const String& label) {
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
}

void ETKT::printLabel(const String& label, int labelLength, int copy,
                      int copies) {
  // home daisy wheel
  this->daisywheel->home(this->settings->getAlignFactor());

  this->feeder->feed(LEAD_FEEDS);
  if (this->stopSignal->shouldStop()) {
    return;
  }

  for (int i = 0; i < labelLength; i++) {
    auto character = Utility::utf8CharAt(label, i);
    // Only press what the wheel actually reached. move() logs the character
    // it could not find and cuts the coil current, which leaves the wheel
    // unreferenced and free to turn; pressing anyway embosses whichever slot
    // it stopped at. readCommandOptions() refuses such a label at the door,
    // so reaching here means a caller inside the device asked for it. A stop
    // is the other reason the wheel may not have got there, and one that
    // lands after it did still keeps the press up.
    if (character != " " &&
        this->daisywheel->move(character, this->settings->getAlignFactor()) &&
        !this->stopSignal->shouldStop()) {
      this->press->press(false, this->settings->getForceFactor(), false);
    }

    this->feeder->feed();
    if (this->stopSignal->shouldStop()) {
      return;
    }
    delay(500);

    this->display->renderProgress(i + 1, label, copy, copies);

    this->lock.lock();
    this->progress = progressPercent(i + 1, labelLength);
    this->lock.unlock();
  }

  if (this->stopSignal->shouldStop()) {
    return;
  }

  // Top the tape up to something the user can take hold of. topUpFeeds()
  // says how far, and why a single letter is left short; the panel works out
  // how many labels fit on the roll from the same rule.
  const int topUp = topUpFeeds(labelLength);
  if (topUp > 0) {
    this->feeder->feed(topUp);
  }

  this->cut();
}
