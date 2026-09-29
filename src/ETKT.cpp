#include "ETKT.h"

#include <Arduino.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "CharacterSet.h"
#include "Configuration.h"
#include "Display.h"
#include "Feeder.h"
#include "Light.h"
#include "Logger.h"
#include "Printhead.h"
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

// The one statement of what commands this device has. A row gives the
// enumerator, the name it answers to on the wire and in /api/<name>, the
// body field its text arrives in or NULL, the facts that hold for it, and
// the handler loop() runs. See CommandSpec for what each one means.
//
// Before this table the same nine commands were written out four times over
// -- a name switch, a factory method each, a dispatch switch with no default
// case, and the route list in Network.cpp -- and home and move had already
// fallen out of the webapp's copy.
const CommandSpec ETKT::COMMANDS[] = {
    {Command::CUT, "cut", NULL, CommandFact::STOPPABLE,
     &ETKT::cutCommandInternal},
    {Command::FEED, "feed", NULL, CommandFact::STOPPABLE,
     &ETKT::feedCommandInternal},
    // A reel is a new roll going in, so it is where the roll's length is
    // declared.
    {Command::REEL, "reel", NULL,
     CommandFact::USES_ROLL_LENGTH | CommandFact::STOPPABLE,
     &ETKT::reelCommandInternal},
    // Align only. This test presses at the minimum force by design -- see
    // Printhead::testPress() -- so a force in the body is ignored, not
    // refused, which keeps a stale cached script.js working.
    {Command::TEST_ALIGN, "testalign", NULL,
     CommandFact::USES_ALIGN | CommandFact::STOPPABLE,
     &ETKT::testCommandInternal},
    {Command::TEST_FULL, "testfull", NULL,
     CommandFact::USES_ALIGN | CommandFact::USES_FORCE |
         CommandFact::STOPPABLE | CommandFact::PRESSES_LABEL,
     &ETKT::testCommandFullInternal},
    // The one command that cannot be stopped: see CommandSpec::stoppable.
    {Command::SAVE, "save", NULL,
     CommandFact::USES_ALIGN | CommandFact::USES_FORCE,
     &ETKT::saveCommandInternal},
    {Command::TAG, "tag", "tag",
     CommandFact::FIELD_IS_LABEL | CommandFact::PRINTS_RUN |
         CommandFact::STOPPABLE | CommandFact::PRESSES_LABEL,
     &ETKT::tagCommandInternal},
    {Command::HOME, "home", NULL, CommandFact::STOPPABLE,
     &ETKT::homeCommandInternal},
    {Command::MOVE, "move", "character", CommandFact::STOPPABLE,
     &ETKT::moveCommandInternal},
    // IDLE is a status, not a job: no handler, and no route is registered for
    // it. It keeps a name because /api/status reports one.
    {Command::IDLE, "idle", NULL, CommandFact::NONE, NULL},
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
           Printhead* printhead, Feeder* feeder, Roll* roll, Sound* sound,
           Light* ledFinish, Light* ledChar, StopSignal* stopSignal) {
  // Upstream never assigned this one, and initialize() dereferences it on its
  // first line. It only ever worked because Logger holds no state, so the
  // uninitialised pointer was never actually read through.
  this->logger = logger;
  this->settings = settings;
  this->display = display;
  this->printhead = printhead;
  this->feeder = feeder;
  this->roll = roll;
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
  this->lastStopId = 0;
  this->calibration.align = 0;
  this->calibration.force = 0;
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
  this->printhead->initialize(this->savedCalibration());
  this->feeder->initialize();
  // Where this boot's stop ids start. See StoppedCommand::id.
  this->lastStopId = (uint32_t)random(0, 1L << 30);
}

Calibration ETKT::savedCalibration() {
  Calibration saved;
  saved.align = (int)this->settings->getAlignFactor();
  saved.force = (int)this->settings->getForceFactor();
  return saved;
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
    const CommandSpec* spec = commandSpec(this->command->command);
    if (spec != NULL && spec->printsRun) {
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
  // A command with no row cannot have been refused a stop by one, and a
  // stop refused is the worse mistake of the two.
  const CommandSpec* spec = commandSpec(this->command->command);
  if (spec != NULL && !spec->stoppable) {
    this->lock.unlock();
    return StopResult::UNSTOPPABLE;
  }
  // Under the lock, so the stop can only land on the command it was meant
  // for: loop() clears it under the same lock as it lets that command go.
  this->stopSignal->raise(StopCause::OPERATOR);
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
  const CommandSpec* spec = commandSpec(this->command->command);
  if (spec == NULL || !spec->printsRun) {
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

  // Picked once, as the job begins, so its presses cannot disagree: the full
  // test used to press its characters at the align it was trialling and cut
  // at the saved one.
  const CommandSpec* spec = commandSpec(running);
  const bool trialsAlign = spec != NULL && spec->usesAlign;
  const bool trialsForce = spec != NULL && spec->usesForce;
  const Calibration saved = this->savedCalibration();
  this->calibration.align = trialsAlign ? this->command->align : saved.align;
  this->calibration.force = trialsForce ? this->command->force : saved.force;

  // Do the task. The command's row says which handler to run. A command with
  // no row at all is a bug worth hearing about; a row with no handler is
  // IDLE, which is a status rather than a job, so it falls straight through
  // to the parking code below.
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
  this->printhead->park();
  this->feeder->deenergize();

  this->lock.lock();
  if (stopped) {
    StoppedCommand record;
    record.id = ++this->lastStopId;
    record.command = running;
    record.cause = this->stopSignal->cause();
    if (spec != NULL && spec->printsRun) {
      record.printed = this->printed;
      record.copies = this->command->copies;
    }
    if (spec != NULL && spec->pressesLabel) {
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
  this->printhead->rest();
  delay(500);
  this->feeder->feed();
  this->ledFinish->off();
  this->ledChar->off();
}

void ETKT::reelCommandInternal() {
  this->display->render(Screen::REELING);
  this->ledFinish->on(LIGHT_FAINT);
  this->printhead->rest();
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
  this->printhead->rest();
  delay(500);

  this->printhead->cut(this->calibration);
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
  // neither, and showing them would be worse than showing nothing: the test
  // press is always at the minimum force, whatever the job's calibration
  // says. See Printhead::testPress().
  display->render(Screen::TESTING);
  ledFinish->off();

  this->printhead->testPress(this->calibration);
}

void ETKT::testCommandFullInternal() {
  this->feedsAtLastCut = this->feeder->feeds();
  this->feeder->feed();
  const std::vector<String> characters = Utility::characters("E-TKT");
  for (size_t i = 0; i < characters.size(); i++) {
    if (this->stopSignal->shouldStop()) {
      return;
    }
    this->feeder->feed();
    this->printhead->stamp(characters[i], this->calibration);
  }
  if (this->stopSignal->shouldStop()) {
    return;
  }
  this->feeder->feed();
  this->printhead->cut(this->calibration);
}

void ETKT::homeCommandInternal() {
  this->ledFinish->on(LIGHT_FULL);
  this->ledChar->on(LIGHT_FULL);
  this->printhead->rest();
  delay(500);
  this->printhead->home(this->calibration);
  delay(1000);
}

void ETKT::moveCommandInternal() {
  this->printhead->rest();
  delay(500);
  this->printhead->turnTo(this->command->label, this->calibration);
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
  this->printhead->rest();
  delay(500);

  if (copies > 1) {
    this->logger->log(String("print ") + label + " x " + copies);
  } else {
    this->logger->log(String("print ") + label);
  }

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

    this->printLabel(label, copy, copies);
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

void ETKT::printLabel(const String& label, int copy, int copies) {
  // What a label may say is CHARACTERS in CharacterSet.h, less the cut
  // mark, plus the space. printableCharacters() is that list; the webapp
  // fetches it rather than keeping one of its own.
  const std::vector<String> characters = Utility::characters(label);
  const int labelLength = characters.size();

  // home daisy wheel
  this->printhead->home(this->calibration);

  this->feeder->feed(LEAD_FEEDS);
  if (this->stopSignal->shouldStop()) {
    return;
  }

  for (int i = 0; i < labelLength; i++) {
    this->printhead->stamp(characters[i], this->calibration);

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

  this->printhead->cut(this->calibration);
}
