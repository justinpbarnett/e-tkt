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
// The whole celebration. Each blink is an off and an on.
static const unsigned long CELEBRATION_MS =
    FINISH_BLINK_TIMES * 2 * FINISH_BLINK_MS + FINISH_FADE_MS;

// How long a job gives the press to get clear of the wheel, once rest() has
// sent it there, before anything moves.
static const unsigned long REST_SETTLE_MS = 500;

// How many times a cut presses the cut mark. The blade does not go all the
// way through the tape either way, so a label comes off with scissors, and
// the cut that ends each label of a run is one press: two more on every
// label were time for nothing. The Cut button cuts once, not once a label,
// so it can take its time, and presses three times, as the machine always
// did.
static const int LABEL_CUT_PRESSES = 1;
static const int CUT_COMMAND_PRESSES = 3;

// The label as the wheel prints it, which has only capitals.
static String asPrinted(const String& label) {
  String printed = label;
  printed.toUpperCase();
  return printed;
}

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
    // An unload undoes a reel: it backs the tape out of the cog, so the roll
    // can come out without being cut or fed the rest of the way through.
    {Command::UNLOAD, "unload", NULL, CommandFact::STOPPABLE,
     &ETKT::unloadCommandInternal},
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
     CommandFact::TEXT_IS_LABEL | CommandFact::PRINTS_RUN |
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
  this->printed = 0;
  this->stoppingAfterLabel = false;
  // Nothing has fed yet, so there is nothing to charge.
  this->accountedFeeds = 0;
  this->feedsAtLabelStart = 0;
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

Calibration ETKT::savedCalibration() const {
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
    // A stop now outranks a stop after the label, which it overtakes.
    if (this->stopSignal->raised()) {
      status.stop = PendingStop::NOW;
    } else if (this->stoppingAfterLabel) {
      status.stop = PendingStop::AFTER_LABEL;
    }
    const CommandSpec* spec = commandSpec(this->command->command);
    if (spec != NULL && spec->printsRun) {
      status.copy = this->runClock.copy();
      status.copies = this->command->copies;
      // Nothing to count down until the run has begun, and nothing but the
      // press finishing its stroke once it is stopping now.
      if (status.copy > 0) {
        status.labelMs = this->runClock.labelMs();
        if (status.stop != PendingStop::NOW) {
          // By the difference, which holds as millis() wraps.
          const long leftMs =
              (long)(this->runClock.endMs(this->stoppingAfterLabel) - millis());
          status.remainingMs = leftMs > 0 ? (uint32_t)leftMs : 0;
        }
      }
    }
  }
  status.stopped = this->lastStopped;
  status.lastCommandId = this->lastCommandId;
  this->lock.unlock();

  return status;
}

void ETKT::submit(const CommandOptions& options, const String& id) {
  // Copied on the way in. The webserver builds its options on the request
  // task's stack and the device needs them to outlive the request, but who
  // owns what should not be part of the interface: a refused command leaves
  // the caller's copy exactly as it found it.
  CommandOptions* incoming = new CommandOptions(options);

  this->lock.lock();
  if (this->command != NULL) {
    this->lock.unlock();
    delete incoming;
    throw PrinterBusyException();
  }
  this->command = incoming;
  this->lastCommandId = id;
  // A new job, so the last one's stop is old news.
  this->lastStopped = StoppedCommand();
  this->submitted.notify_one();
  this->lock.unlock();
}

bool ETKT::isForLastCommand(const String& id) const {
  return id.length() == 0 || id == this->lastCommandId;
}

StopResult ETKT::stop(const String& id) {
  this->lock.lock();
  // Before anything else, and under the lock, so a command that arrives as
  // its stop does is either the one the stop finds or one it leaves alone.
  if (!this->isForLastCommand(id)) {
    this->lock.unlock();
    return StopResult::NOT_THE_COMMAND;
  }
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

StopResult ETKT::stopAfterLabel(const String& id) {
  this->lock.lock();
  if (!this->isForLastCommand(id)) {
    this->lock.unlock();
    return StopResult::NOT_THE_COMMAND;
  }
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
  // looks for one before it sleeps, so a command submitted before loop() got
  // here is taken at once rather than half a second late.
  std::unique_lock<std::mutex> guard(this->lock);
  this->submitted.wait_for(guard, std::chrono::milliseconds(500),
                           [this] { return this->command != NULL; });

  // Take a copy of which command it is while the lock is held. Only this
  // function ever lets a command go, so reading it again after the unlock
  // would in fact be safe today -- but that is a fact about the rest of the
  // class, not about this code, and the next code to set this->command would
  // silently break it. The enum is two bytes; copy it out.
  if (this->command == NULL) {
    return;
  }
  const Command running = this->command->command;
  guard.unlock();

  // Picked once, as the job begins, so its presses cannot disagree: the full
  // test used to press its characters at the align it was trialling and cut
  // at the saved one. Field by field, because a row can read one value
  // without the other: the align test trials an align at the saved force.
  const CommandSpec* spec = commandSpec(running);
  const bool trialsAlign = spec != NULL && spec->usesAlign;
  const bool trialsForce = spec != NULL && spec->usesForce;
  const Calibration saved = this->savedCalibration();
  this->calibration.align = trialsAlign ? this->command->align : saved.align;
  this->calibration.force = trialsForce ? this->command->force : saved.force;
  // Tape fed before this command is not its label.
  this->feedsAtLabelStart = this->feeder->feeds();

  // Do the task. The command's row says which handler to run. A command with
  // no row at all is a bug worth hearing about; a row with no handler is
  // IDLE, which is a status rather than a job, so it falls straight through
  // to the parking code below.
  if (spec == NULL) {
    this->logger->log(String("No table row for command ") + (int)running);
  } else if (spec->run != NULL) {
    (this->*(spec->run))();
  }

  // Whether a stop cut this command short, as against arriving while it was
  // finishing anyway.
  const bool stopped = this->stopSignal->cutShort();
  if (stopped) {
    this->logger->log(String("Stopped ") + commandName(running));
  }

  // Park the motors before the command is let go, and outside the lock,
  // which used to be held through all of this and stalled every status poll
  // from the web task for as long as it took. Parking first closes a gap:
  // submit() goes on refusing new work until the machine is genuinely no
  // longer busy, so nothing can begin against a machine that is still being
  // put away.
  this->printhead->park();
  this->feeder->deenergize();

  // Four commands feed, and every feed is tape off the roll. Charged once
  // the feeder has let go, so a feed it halted partway is counted, and
  // before the command is let go, so the first idle status already shows it.
  this->accountForTape();

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
      // Tape fed since the label began is a label left on the tape.
      record.unfinished = this->feeder->feeds() > this->feedsAtLabelStart;
    }
    this->lastStopped = record;
  }
  delete this->command;
  this->command = NULL;
  this->progress = 0;
  this->printed = 0;
  this->stoppingAfterLabel = false;
  this->runClock = RunClock();
  // Down again before the next command can be submitted, so no stop
  // outlives its command.
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
  delay(REST_SETTLE_MS);
  this->feeder->feed();
  this->ledFinish->off();
  this->ledChar->off();
}

void ETKT::reelCommandInternal() {
  this->display->render(Screen::REELING);
  this->ledFinish->on(LIGHT_FAINT);
  this->printhead->rest();
  delay(REST_SETTLE_MS);

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

void ETKT::unloadCommandInternal() {
  this->display->render(Screen::UNLOADING);
  this->ledFinish->on(LIGHT_FAINT);
  this->printhead->rest();
  delay(REST_SETTLE_MS);

  // As far back as a reel threads the tape forward: from past the cutter to
  // behind the cog. The roll's count is left as it was, and Feeder::feeds()
  // says why.
  this->feeder->backOut(REEL_FEEDS);
  this->ledFinish->off();
  this->ledChar->off();
}

void ETKT::cutCommandInternal() {
  this->display->render(Screen::CUTTING);
  this->ledChar->on(LIGHT_DIM);
  this->printhead->rest();
  delay(REST_SETTLE_MS);

  this->printhead->cut(this->calibration, CUT_COMMAND_PRESSES);
  ledChar->off();
}

void ETKT::saveCommandInternal() {
  this->logger->log("saving settings");

  // The job's calibration is the pair this save was sent, since its row says
  // it reads both.
  display->renderSaved(this->calibration);
  // The waits used to live inside the two renderers. They are the caller's
  // business: how long a confirmation stays up is a decision about this
  // command, not about how to draw a screen.
  delay(SAVED_SCREEN_MS);
  ledFinish->off();

  settings->save(this->calibration.align, this->calibration.force);
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
  // The label " E-TKT", pressed and cut like any other: a blank feed and a
  // space ahead of the text, so the E lands two feeds in, and no top-up.
  const String label = " E-TKT";
  this->display->renderProgress(0, label, 1, 1);
  this->printLabel(label, 1, 1);
}

void ETKT::homeCommandInternal() {
  this->ledFinish->on(LIGHT_FULL);
  this->ledChar->on(LIGHT_FULL);
  this->printhead->rest();
  delay(REST_SETTLE_MS);
  this->printhead->home(this->calibration);
  delay(1000);
}

void ETKT::moveCommandInternal() {
  this->printhead->rest();
  delay(REST_SETTLE_MS);
  this->printhead->turnTo(this->command->label, this->calibration);
}

void ETKT::tagCommandInternal() {
  const String label = asPrinted(this->command->label);
  const int copies = this->command->copies;
  // At the calibration the run presses at, before anything moves.
  const RunEstimate expected =
      this->estimate(*this->command, this->calibration);
  // On its first label from the start, not from its first feed. The panel
  // polls throughout, and "label 0 of 3" while the press settles is nothing
  // an operator can make sense of.
  this->lock.lock();
  this->runClock.start(expected, copies, millis());
  this->lock.unlock();
  // enables servo
  this->printhead->rest();
  delay(REST_SETTLE_MS);

  if (copies > 1) {
    this->logger->log(String("print ") + label + " x " + copies);
  } else {
    this->logger->log(String("print ") + label);
  }

  this->ledChar->on(LIGHT_DIM);

  for (int copy = 1; copy <= copies; copy++) {
    this->lock.lock();
    this->runClock.labelStarted(copy, millis());
    this->progress = 0;
    this->lock.unlock();

    this->display->renderProgress(0, label, copy, copies);

    // Once a run, not once a label. The tune says printing has started, and
    // the same few seconds of it before every label of a long run would be
    // most of a minute of music for nothing.
    if (copy == 1) {
      this->sound->playTune(label);
      // Before any tape moves, so a wheel that cannot find its magnet stops
      // the run with nothing on the tape to cut off. Every character homes
      // again on its way to its slot, so this is the only home a run needs
      // of its own.
      this->printhead->home(this->calibration);
    }

    this->printLabel(label, copy, copies);
    this->accountForTape();
    if (this->stopSignal->cutShort()) {
      // Stopped partway through this label, which is left on the tape.
      break;
    }
    this->feedsAtLabelStart = this->feeder->feeds();
    this->lock.lock();
    this->printed = copy;
    this->lock.unlock();
    if (copy == copies) {
      break;
    }

    // Between one label's end and the next one's first feed, where both
    // kinds of stop can end a run without leaving anything unfinished on the
    // tape. A stop now, obeyed here, still counts as cutting the run short.
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
  // A stop that came as the last label was being finished was too late to
  // cut anything short. The operator still asked for the machine to stop, so it
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

void ETKT::printLabel(const String& label, int copy, int copies) {
  // What a label may say is CHARACTERS in CharacterSet.h, less the cut
  // mark, plus the space. printableCharacters() is that list; the webapp
  // fetches it rather than keeping one of its own.
  const std::vector<String> characters = Utility::characters(label);
  const int labelLength = characters.size();

  // The tape runs one feed ahead of the press from here on. Each feed is
  // started as the character before it is done, and the wheel turns to the
  // next character while it runs. The press waits for both.
  this->feeder->start(LEAD_FEEDS);

  for (int i = 0; i < labelLength; i++) {
    this->printhead->stamp(characters[i], this->calibration);
    // A character that was pressed has waited for the tape already. A space
    // is only its feed, so it waits here. The screen is drawn next, and a
    // redraw stalls a feed that is still under way.
    this->feeder->finish();
    if (this->stopSignal->shouldStop()) {
      return;
    }

    this->display->renderProgress(i + 1, label, copy, copies);

    this->lock.lock();
    this->progress = progressPercent(i + 1, labelLength);
    this->lock.unlock();

    this->feeder->start(1);
  }

  // Top the tape up to something the user can take hold of. topUpFeeds()
  // says how far, and why a single letter is left short; the panel works out
  // how many labels fit on the roll from the same rule. The top-up follows
  // the last character's feed, and the wheel turns to the cut mark while
  // both run. A stop that is up by now asks for no more tape, and turns the
  // wheel nowhere.
  this->feeder->start(topUpFeeds(labelLength));
  if (this->command->cut) {
    this->printhead->cut(this->calibration, LABEL_CUT_PRESSES);
  }
  // Without the cut, the tape is still on its way to the end of the label.
  this->feeder->finish();
}

void ETKT::RunClock::start(const RunEstimate& expected, int copies,
                           unsigned long nowMs) {
  this->expected = expected;
  this->copies = copies;
  this->current = 1;
  this->startMs = nowMs;
}

void ETKT::RunClock::labelStarted(int copy, unsigned long nowMs) {
  this->current = copy;
  this->labelStartMs = nowMs;
  if (copy == 2) {
    this->secondLabelStartMs = nowMs;
  }
}

int ETKT::RunClock::copy() const { return this->current; }

uint32_t ETKT::RunClock::labelMs() const {
  // The first label waits for the tune and the home, and turns the wheel
  // from where the home left it rather than from where the last label did,
  // so only the labels from the second on are timed.
  if (this->current < 3) {
    return this->expected.labelMs;
  }
  return (this->labelStartMs - this->secondLabelStartMs) / (this->current - 2);
}

unsigned long ETKT::RunClock::endMs(bool endsWithThisLabel) const {
  const int lastCopy = endsWithThisLabel ? this->current : this->copies;
  if (this->current < 2) {
    return this->startMs + this->expected.runMs -
           (this->copies - lastCopy) * this->expected.labelMs;
  }
  const int labelsLeft = lastCopy - this->current + 1;
  return this->labelStartMs + labelsLeft * this->labelMs() + CELEBRATION_MS;
}

RunEstimate ETKT::estimate(const CommandOptions& options) const {
  return this->estimate(options, this->savedCalibration());
}

RunEstimate ETKT::estimate(const CommandOptions& options,
                           const Calibration& calibration) const {
  RunEstimate expected;
  const CommandSpec* spec = commandSpec(options.command);
  if (spec == NULL || !spec->printsRun) {
    return expected;
  }
  const String label = asPrinted(options.label);
  const std::vector<String> characters = Utility::characters(label);

  // The first label starts from the home, and leaves the wheel where every
  // label after it starts.
  String wheel = CHAR_HOME_CHARACTER;
  const uint64_t firstUs =
      this->labelUs(characters, options.cut, calibration, &wheel);
  const uint64_t nextUs =
      this->labelUs(characters, options.cut, calibration, &wheel);
  const uint64_t nextLabels = options.copies > 1 ? options.copies - 1 : 0;
  const uint64_t runUs = REST_SETTLE_MS * 1000 + this->sound->tuneUs(label) +
                         this->printhead->homeUs(calibration) + firstUs +
                         nextLabels * nextUs + CELEBRATION_MS * 1000;

  expected.labelMs = (uint32_t)((nextUs + 500) / 1000);
  expected.runMs = (uint32_t)((runUs + 500) / 1000);
  return expected;
}

uint64_t ETKT::labelUs(const std::vector<String>& characters, bool cut,
                       const Calibration& calibration, String* wheel) const {
  // As printLabel() runs them: the lead feeds up to the first character, and
  // each character after it waits for the one feed started as the character
  // before it was done.
  uint64_t us = 0;
  int feeds = LEAD_FEEDS;
  for (const String& character : characters) {
    us += this->printhead->stampUs(wheel, character,
                                   this->feeder->feedUs(feeds), calibration);
    feeds = 1;
  }
  // The top-up runs on from the last character's feed, and the cut waits for
  // both.
  feeds += topUpFeeds(characters.size());
  if (!cut) {
    return us + this->feeder->feedUs(feeds);
  }
  return us + this->printhead->cutUs(wheel, this->feeder->feedUs(feeds),
                                     calibration, LABEL_CUT_PRESSES);
}
