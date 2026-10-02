// Host-side tests for the job runner, ETKT, through the calls the rest of
// the firmware makes on it: submit, repeat, stop, createStatus, busy, loop
// and showIdle.
//
// Every stop rule the operator relies on lives in ETKT -- what can be
// stopped, what a stop leaves on the tape, what the panel is told afterwards
// -- and until these tests the only way to check one was a real machine and
// a real roll of tape. The machine here is the real one, module for module,
// with fakes where it meets the hardware: a HostMachine, which the Api's
// tests build too. A stop arrives the way it does on the board, from outside
// the job while the job waits or turns a motor.
//
// Run with:  pio test -e native
#include <unity.h>

#include <utility>
#include <vector>

#include "Arduino.h"
#include "HostMachine.h"

static HostMachine* machine;

// The machine's modules by name, so a test reads as it would against the
// modules themselves.
static FakeServo* pressServo;
static FakeStepper* charStepper;
static FakeStepper* feedStepper;
static FakeDisplay* display;
static FakeMagnet* magnet;
static StrokeLog* strokes;

static Logger* logger;
static Settings* settings;
static Roll* roll;
static Feeder* feeder;
static ETKT* etkt;

// Builds the machine, which boots it, and names its modules.
static void boot(void) {
  machine = new HostMachine();
  pressServo = &machine->pressServo;
  charStepper = &machine->charStepper;
  feedStepper = &machine->feedStepper;
  display = &machine->display;
  magnet = &machine->magnet;
  strokes = &machine->strokes;
  logger = &machine->logger;
  settings = &machine->settings;
  roll = &machine->roll;
  feeder = &machine->feeder;
  etkt = &machine->etkt;
}

void setUp(void) {
  stubReset();
  boot();
}

void tearDown(void) { delete machine; }

// The machine switched off and on again: a new one over the same flash.
static void reboot(void) {
  delete machine;
  boot();
}

static void submit(Command command) {
  CommandOptions options;
  options.command = command;
  etkt->submit(options);
}

static void submitTag(const String& label, int copies) {
  etkt->submit(tagOptions(label, copies));
}

static bool refused(Command command) {
  try {
    submit(command);
  } catch (const PrinterBusyException&) {
    return true;
  }
  return false;
}

static bool repeatRefused(void) {
  try {
    etkt->repeat();
  } catch (const PrinterBusyException&) {
    return true;
  }
  return false;
}

// --- running a job -------------------------------------------------------

void test_a_submitted_feed_runs_and_the_machine_goes_idle(void) {
  submit(Command::FEED);
  TEST_ASSERT_EQUAL_INT(Command::FEED, etkt->createStatus().currentCommand);

  etkt->loop();

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.currentCommand);
  // One feed comes off the roll, and the panel's tape gauge reads it here.
  TEST_ASSERT_EQUAL_UINT32(1, status.roll.feedsUsed);
}

void test_the_machine_is_busy_from_taking_a_job_until_the_job_ends(void) {
  // What the button on the machine goes by, a hundred times a second, to
  // tell a press that stops a job from one that starts one.
  TEST_ASSERT_FALSE(etkt->busy());
  submit(Command::FEED);
  TEST_ASSERT_TRUE(etkt->busy());
  static bool busyThroughout;
  busyThroughout = true;
  stubAfterDelay() = [] { busyThroughout = busyThroughout && etkt->busy(); };

  etkt->loop();

  TEST_ASSERT_TRUE(busyThroughout);
  TEST_ASSERT_FALSE(etkt->busy());
}

// A run of labels, start to finish. Each label of "AB" is seven feeds --
// the lead, one per character, and four more to make it long enough to
// hold -- and the tape gauge counts every one.
void test_a_finished_run_shows_finished_and_records_no_stop(void) {
  submitTag("AB", 2);

  etkt->loop();

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.currentCommand);
  TEST_ASSERT_EQUAL_UINT32(14, status.roll.feedsUsed);
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.stopped.command);
  const DisplayCall* progress = display->last(DisplayCall::RENDER_PROGRESS);
  TEST_ASSERT_EQUAL_INT(2, progress->copy);
  TEST_ASSERT_EQUAL_INT(2, progress->copies);
  const std::vector<Screen> screens = display->screens();
  TEST_ASSERT_EQUAL_INT(1, (int)screens.size());
  TEST_ASSERT_EQUAL_INT((int)Screen::FINISHED, (int)screens[0]);
  TEST_ASSERT_FALSE(display->last(DisplayCall::RENDER_IDLE)->stopped);
}

// What the panel shows while a run prints: which label of how many, and
// how far into it. The panel polls once a second and can land anywhere, so
// every status the run reports has to make sense -- "label 0 of 3" while
// the press settles did not.
void test_a_run_reports_which_label_it_is_on_and_how_far_into_it(void) {
  submitTag("AB", 3);
  static std::vector<std::pair<int, int>> seen;
  static int wrongCopies;
  seen.clear();
  wrongCopies = 0;
  stubAfterDelay() = [] {
    const StatusUpdate status = etkt->createStatus();
    if (status.currentCommand != Command::TAG) {
      return;
    }
    if (status.copies != 3) {
      wrongCopies++;
    }
    const std::pair<int, int> now(status.copy, status.progress);
    if (seen.empty() || seen.back() != now) {
      seen.push_back(now);
    }
  };

  etkt->loop();

  TEST_ASSERT_EQUAL_INT(0, wrongCopies);
  // Half of "AB" is 50%. A finished label reads 99 rather than 100 until
  // the run is over, so the bar never says done while the press still moves.
  const int expected[][2] = {{1, 0},  {1, 50}, {1, 99}, {2, 0}, {2, 50},
                             {2, 99}, {3, 0},  {3, 50}, {3, 99}};
  const size_t count = sizeof(expected) / sizeof(expected[0]);
  TEST_ASSERT_EQUAL_INT((int)count, (int)seen.size());
  for (size_t i = 0; i < count; i++) {
    TEST_ASSERT_EQUAL_INT(expected[i][0], seen[i].first);
    TEST_ASSERT_EQUAL_INT(expected[i][1], seen[i].second);
  }
}

// Most of a label is tape moving, and the wheel used to wait for it. Now it
// turns to each character, and to the cut mark, while the tape feeds up to
// it, and the press waits for both.
void test_the_wheel_turns_while_the_tape_feeds_up_to_each_character(void) {
  submitTag("AB", 1);
  // Whether any step of the wheel on its way to each stroke was taken while
  // the tape was moving, by the stroke it was on its way to.
  static std::vector<bool> turnedWhileFeeding;
  turnedWhileFeeding.clear();
  charStepper->afterStep = [] {
    const size_t stroke = strokes->strokes.size();
    if (turnedWhileFeeding.size() <= stroke) {
      turnedWhileFeeding.resize(stroke + 1, false);
    }
    if (feedStepper->distanceToGo() != 0) {
      turnedWhileFeeding[stroke] = true;
    }
  };

  etkt->loop();

  // A, B and the cut.
  TEST_ASSERT_EQUAL_INT(3, (int)strokes->strokes.size());
  TEST_ASSERT_EQUAL_INT(3, (int)turnedWhileFeeding.size());
  for (size_t i = 0; i < turnedWhileFeeding.size(); i++) {
    TEST_ASSERT_TRUE_MESSAGE(turnedWhileFeeding[i], "the wheel waited");
  }
  // The same tape as ever.
  TEST_ASSERT_EQUAL_UINT32(7, etkt->createStatus().roll.feedsUsed);
}

// The cutter never goes all the way through the tape, so the labels of a
// run come off with scissors anyway, and a run can leave the cut out. Each
// label still takes the same tape, so the scissors have the same margins to
// cut between.
void test_a_run_can_leave_out_the_cut(void) {
  etkt->submit(tagOptions("AB", 2, false));

  etkt->loop();

  // A and B, twice, and nothing at the cut mark.
  TEST_ASSERT_EQUAL_INT(4, (int)strokes->strokes.size());
  TEST_ASSERT_EQUAL_UINT32(14, etkt->createStatus().roll.feedsUsed);
}

// The cut that ends a label is one press: the blade does not go all the way
// through the tape either way, so the labels of a run come off with
// scissors. The Cut button cuts once, not once a label, so it can take its
// time, and presses the cut mark three times, as the machine always did.
void test_the_cut_button_presses_three_times_and_a_label_once(void) {
  submitTag("AB", 2);
  etkt->loop();
  // A, B and the cut, twice.
  TEST_ASSERT_EQUAL_INT(6, (int)strokes->strokes.size());
  const long cutMark = strokes->strokes.back().bearing;
  strokes->strokes.clear();

  submit(Command::CUT);
  etkt->loop();

  TEST_ASSERT_EQUAL_INT(3, (int)strokes->strokes.size());
  for (const Stroke& stroke : strokes->strokes) {
    TEST_ASSERT_EQUAL_INT32(cutMark, stroke.bearing);
  }
}

// A new roll is declared as it goes in, and threading it through to the
// cutter is the first tape off it.
void test_a_reel_loads_a_roll_of_the_declared_length(void) {
  CommandOptions options;
  options.command = Command::REEL;
  options.rollLengthMm = 5000;
  etkt->submit(options);

  etkt->loop();

  const RollState roll = etkt->createStatus().roll;
  TEST_ASSERT_EQUAL_UINT32(5000, roll.lengthMm);
  TEST_ASSERT_EQUAL_UINT32(16, roll.feedsUsed);
}

// Most rolls are the same length as the last one, so the panel lets the
// length be left out. The tape fed before the reel was off the old roll.
void test_a_reel_without_a_length_takes_the_last_roll_length(void) {
  CommandOptions options;
  options.command = Command::REEL;
  options.rollLengthMm = 5000;
  etkt->submit(options);
  etkt->loop();
  submit(Command::FEED);
  etkt->loop();

  submit(Command::REEL);
  etkt->loop();

  const RollState roll = etkt->createStatus().roll;
  TEST_ASSERT_EQUAL_UINT32(5000, roll.lengthMm);
  TEST_ASSERT_EQUAL_UINT32(16, roll.feedsUsed);
}

// A roll that has been threaded comes out again without being cut or fed
// the rest of the way through: an unload backs the tape out of the cog as
// far as a reel threads it, and lets go of it.
void test_an_unload_backs_the_tape_out_as_far_as_a_reel_threads_it(void) {
  submit(Command::REEL);
  etkt->loop();
  TEST_ASSERT_TRUE(feedStepper->currentPosition() != 0);
  display->clear();

  submit(Command::UNLOAD);
  etkt->loop();

  TEST_ASSERT_EQUAL_INT32(0, feedStepper->currentPosition());
  TEST_ASSERT_FALSE(feedStepper->energized);
  TEST_ASSERT_EQUAL_INT((int)Screen::UNLOADING,
                        (int)display->screens().front());
  TEST_ASSERT_EQUAL_INT(Command::IDLE, etkt->createStatus().currentCommand);
}

// What an unload backs out is still on the roll, but how much of it came
// back cannot be told, so the roll's count is left as it was: see
// Feeder::feeds().
void test_an_unload_leaves_the_roll_count_as_it_was(void) {
  CommandOptions options;
  options.command = Command::REEL;
  options.rollLengthMm = 5000;
  etkt->submit(options);
  etkt->loop();
  submit(Command::FEED);
  etkt->loop();

  submit(Command::UNLOAD);
  etkt->loop();

  const RollState roll = etkt->createStatus().roll;
  TEST_ASSERT_EQUAL_UINT32(5000, roll.lengthMm);
  TEST_ASSERT_EQUAL_UINT32(17, roll.feedsUsed);
}

// A stop halts an unload where the tape is and lets go of it, so the rest
// can be pulled out by hand. Nothing was pressed, so nothing is left on the
// tape to cut off. The stop is on record for the panel, and the machine's own
// screen asks for the next roll: see the roll change, below.
void test_a_stopped_unload_lets_go_of_the_tape_and_leaves_nothing_to_cut(void) {
  submit(Command::REEL);
  etkt->loop();
  static long threaded;
  threaded = feedStepper->currentPosition();
  feedStepper->afterStep = [] {
    if (feedStepper->currentPosition() != threaded) {
      etkt->stop();
    }
  };

  submit(Command::UNLOAD);
  etkt->loop();

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::UNLOAD, (int)status.stopped.command);
  TEST_ASSERT_FALSE(status.stopped.unfinished);
  TEST_ASSERT_FALSE(feedStepper->energized);
  TEST_ASSERT_TRUE(feedStepper->currentPosition() != 0);
}

// Saving is the one job that ends in a reboot. The calibration has to be in
// the EEPROM before the restart, or the machine comes back up without it.
void test_saving_stores_the_calibration_and_reboots(void) {
  CommandOptions options;
  options.command = Command::SAVE;
  options.align = 7;
  options.force = 3;
  etkt->submit(options);

  etkt->loop();

  const DisplayCall* saved = display->last(DisplayCall::RENDER_SAVED);
  TEST_ASSERT_NOT_NULL(saved);
  TEST_ASSERT_EQUAL_INT(7, saved->calibration.align);
  TEST_ASSERT_EQUAL_INT(3, saved->calibration.force);
  TEST_ASSERT_EQUAL_INT((int)Screen::REBOOTING, (int)display->screens().back());
  TEST_ASSERT_EQUAL_INT(1, stubRestarts());
  // What the machine reads when it comes back up.
  Settings rebooted(logger);
  rebooted.initialize();
  TEST_ASSERT_EQUAL_INT(7, rebooted.getAlignFactor());
  TEST_ASSERT_EQUAL_INT(3, rebooted.getForceFactor());
}

// A job is one at a time. A second tap while the first job waits its turn
// must not replace it: the first tap is the one the operator is watching.
void test_a_second_job_is_refused_while_the_first_waits_its_turn(void) {
  submit(Command::FEED);

  TEST_ASSERT_TRUE(refused(Command::CUT));
  TEST_ASSERT_EQUAL_INT(Command::FEED, etkt->createStatus().currentCommand);

  etkt->loop();
  TEST_ASSERT_FALSE(refused(Command::CUT));
  etkt->loop();
}

// Putting the machine away ends with a full redraw of the idle screen, QR
// code and all. By then the motors are parked and the job is over, so a job
// posted while it draws is one the machine can take. A 409 then would be the
// panel told the machine is busy when all it is doing is drawing.
void test_a_job_posted_while_the_idle_screen_draws_is_taken(void) {
  submit(Command::FEED);
  static bool refusedWhileIdleDrew;
  refusedWhileIdleDrew = true;
  display->onCall = [](const DisplayCall& call) {
    if (call.kind == DisplayCall::RENDER_IDLE) {
      refusedWhileIdleDrew = refused(Command::CUT);
    }
  };

  etkt->loop();

  TEST_ASSERT_FALSE(refusedWhileIdleDrew);
  display->onCall = nullptr;
  etkt->loop();
}

// The OLED is started once, at boot. Starting it again partway through a
// job blanks the glass and sets its contrast back, and the save
// confirmation used to do that on its way up.
void test_no_job_starts_the_screen_again(void) {
  for (size_t i = 0; i < ETKT::COMMAND_COUNT; i++) {
    if (ETKT::COMMANDS[i].run == NULL) {
      continue;
    }
    CommandOptions options;
    options.command = ETKT::COMMANDS[i].command;
    options.label = "A";
    options.align = 5;
    options.force = 5;
    etkt->submit(options);
    etkt->loop();
  }

  TEST_ASSERT_EQUAL_INT(0, display->countOf(DisplayCall::INITIALIZE));
}

// The full test trials an align and a force before either is saved, and
// the cut that ends it is part of the trial. It used to cut at the saved
// align, so the tape it handed back was pressed at one align and cut at
// another, and an operator winding the align up to find the cut mark never
// saw the cut move.
void test_the_full_test_cuts_at_the_align_it_is_testing(void) {
  submit(Command::CUT);
  etkt->loop();
  const long cutAtFive = strokes->strokes.back().bearing;
  settings->save(9, 5);
  submit(Command::CUT);
  etkt->loop();
  const long cutAtNine = strokes->strokes.back().bearing;
  // Or the two aligns land in the same place and this test proves nothing.
  TEST_ASSERT_NOT_EQUAL(cutAtFive, cutAtNine);
  settings->save(5, 5);

  CommandOptions options;
  options.command = Command::TEST_FULL;
  options.align = 9;
  options.force = 5;
  etkt->submit(options);
  etkt->loop();

  TEST_ASSERT_EQUAL_INT32(cutAtNine, strokes->strokes.back().bearing);
}

// --- stopping -------------------------------------------------------------

// A tap on stop can race the end of the job it was meant for. Arriving to
// find nothing running is not an error, and the stop must not lie in wait
// for the next job, which nobody asked to stop.
void test_a_stop_with_nothing_running_stops_nothing_later(void) {
  TEST_ASSERT_EQUAL_INT((int)StopResult::IDLE, (int)etkt->stop());
  TEST_ASSERT_EQUAL_INT((int)StopResult::IDLE, (int)etkt->stopAfterLabel());

  submit(Command::FEED);
  etkt->loop();

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.stopped.command);
  TEST_ASSERT_EQUAL_UINT32(1, status.roll.feedsUsed);
}

// Saving writes the calibration and reboots. Nothing moves, and a save cut
// off partway would leave half a calibration behind, so it is refused.
void test_saving_cannot_be_stopped(void) {
  CommandOptions options;
  options.command = Command::SAVE;
  options.align = 7;
  options.force = 3;
  etkt->submit(options);

  TEST_ASSERT_EQUAL_INT((int)StopResult::UNSTOPPABLE, (int)etkt->stop());
  TEST_ASSERT_EQUAL_INT((int)PendingStop::NONE, (int)etkt->createStatus().stop);

  etkt->loop();
  TEST_ASSERT_EQUAL_INT(1, stubRestarts());
}

// The panel offers the stop button by what the table says, so the table
// has to say what stop() does: anything but a save stops. A save moves
// nothing and ends in a reboot, so there is nothing to stop.
void test_every_command_but_saving_can_be_stopped(void) {
  for (size_t i = 0; i < ETKT::COMMAND_COUNT; i++) {
    const CommandSpec& spec = ETKT::COMMANDS[i];
    if (spec.run == NULL) {
      continue;
    }
    const bool expected = spec.command != Command::SAVE;
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)expected, (int)spec.stoppable,
                                  spec.name);
    CommandOptions options;
    options.command = spec.command;
    options.label = "A";
    options.align = 5;
    options.force = 5;
    etkt->submit(options);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        (int)(expected ? StopResult::STOPPING : StopResult::UNSTOPPABLE),
        (int)etkt->stop(), spec.name);
    etkt->loop();
  }
}

// Only a run of labels has a label to stop after, and only a run says how
// many labels it has. A feed asked to stop after its label would otherwise
// say "stopping" and then do nothing. The panel offers the stop after this
// label, and counts the labels, by what the table says.
void test_only_a_run_of_labels_can_stop_after_a_label(void) {
  for (size_t i = 0; i < ETKT::COMMAND_COUNT; i++) {
    const CommandSpec& spec = ETKT::COMMANDS[i];
    if (spec.run == NULL) {
      continue;
    }
    const bool expected = spec.command == Command::TAG;
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)expected, (int)spec.printsRun,
                                  spec.name);
    CommandOptions options;
    options.command = spec.command;
    options.label = "A";
    options.copies = 2;
    options.align = 5;
    options.force = 5;
    etkt->submit(options);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        (int)(expected ? StopResult::STOPPING : StopResult::UNSTOPPABLE),
        (int)etkt->stopAfterLabel(), spec.name);
    const StatusUpdate status = etkt->createStatus();
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        (int)(expected ? PendingStop::AFTER_LABEL : PendingStop::NONE),
        (int)status.stop, spec.name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected ? 2 : 0, status.copies, spec.name);
    etkt->loop();
  }
}

// The emergency stop. It lands between two steps of the wheel, partway into
// the first label of three, and the panel is then told what was cut short:
// no label finished, and one begun and left on the tape for the operator to
// cut off before the next.
void test_a_run_stopped_partway_through_a_label_leaves_it_on_the_tape(void) {
  submitTag("AB", 3);
  // While the lead feed moves the tape, so the label has begun: the wheel is
  // on its way to the first character.
  charStepper->afterStep = [] {
    if (feedStepper->distanceToGo() != 0) {
      etkt->stop();
    }
  };

  etkt->loop();

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.currentCommand);
  TEST_ASSERT_EQUAL_INT(Command::TAG, status.stopped.command);
  TEST_ASSERT_TRUE(status.stopped.cause == StopCause::OPERATOR);
  TEST_ASSERT_EQUAL_INT(0, status.stopped.printed);
  TEST_ASSERT_EQUAL_INT(3, status.stopped.copies);
  TEST_ASSERT_TRUE(status.stopped.unfinished);
  // The press never left rest: nothing came down on a character the wheel
  // never reached.
  TEST_ASSERT_EQUAL_INT(REST_ANGLE, pressServo->minAngle());
  TEST_ASSERT_TRUE(display->last(DisplayCall::RENDER_IDLE)->stopped);
}

// A stop leaves a label on the tape only if the command was pressing one.
// A reel feeds tape too, but nothing is pressed into it, and sending the
// operator to cut off a label that is not there has them cut good tape off
// a new roll. Each command here is stopped once its tape has moved.
void test_only_a_command_that_presses_a_label_leaves_one_unfinished(void) {
  static long fedBefore;
  feedStepper->afterStep = [] {
    if (feeder->feeds() > fedBefore) {
      etkt->stop();
    }
  };
  const Command commands[] = {Command::REEL, Command::TEST_FULL, Command::TAG};
  const bool pressesLabel[] = {false, true, true};

  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    const CommandSpec* spec = commandSpec(commands[i]);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)pressesLabel[i], (int)spec->pressesLabel,
                                  spec->name);
    fedBefore = feeder->feeds();
    CommandOptions options;
    options.command = commands[i];
    options.label = "AB";
    options.align = 5;
    options.force = 5;
    etkt->submit(options);
    etkt->loop();

    const StatusUpdate status = etkt->createStatus();
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)commands[i], (int)status.stopped.command,
                                  spec->name);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)pressesLabel[i],
                                  (int)status.stopped.unfinished, spec->name);
  }
}

// A wheel that turned without finding its magnet ends the run the way the
// stop button does, and the panel is told it was the wheel. Pressing on
// would put every character after it in the wrong slot.
void test_a_lost_wheel_ends_the_run_and_says_why(void) {
  magnet->present = false;
  submitTag("AB", 3);

  etkt->loop();

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::TAG, status.stopped.command);
  TEST_ASSERT_TRUE(status.stopped.cause == StopCause::LOST_WHEEL);
  TEST_ASSERT_EQUAL_INT(0, status.stopped.printed);
  // Found out by the home before the lead feed, so no tape has moved and
  // there is nothing to cut off.
  TEST_ASSERT_FALSE(status.stopped.unfinished);
  TEST_ASSERT_EQUAL_INT(REST_ANGLE, pressServo->minAngle());
  TEST_ASSERT_FALSE(charStepper->energized);
  TEST_ASSERT_TRUE(display->last(DisplayCall::RENDER_IDLE)->stopped);
}

// Two stops in a row can say the same thing: two homes of a wheel that has
// lost its magnet, one after the other. The panel tells stops apart by id,
// so an operator who dismissed the first still hears about the second.
void test_each_stop_is_told_apart_from_the_last(void) {
  magnet->present = false;
  submit(Command::HOME);
  etkt->loop();
  const StoppedCommand first = etkt->createStatus().stopped;

  submit(Command::HOME);
  etkt->loop();

  const StoppedCommand second = etkt->createStatus().stopped;
  TEST_ASSERT_TRUE(second.cause == StopCause::LOST_WHEEL);
  TEST_ASSERT_TRUE(first.id != 0);
  TEST_ASSERT_TRUE(second.id != 0);
  TEST_ASSERT_TRUE(first.id != second.id);
}

// The panel's stop button says which stop is on its way. A stop now
// overtakes a stop after the label: an operator who asked for the gentle
// one and then saw something go wrong gets the hard one.
void test_a_stop_now_overtakes_a_stop_after_the_label(void) {
  submitTag("AB", 3);

  TEST_ASSERT_EQUAL_INT((int)StopResult::STOPPING, (int)etkt->stopAfterLabel());
  TEST_ASSERT_EQUAL_INT((int)PendingStop::AFTER_LABEL,
                        (int)etkt->createStatus().stop);
  TEST_ASSERT_EQUAL_INT((int)StopResult::STOPPING, (int)etkt->stop());
  TEST_ASSERT_EQUAL_INT((int)PendingStop::NOW, (int)etkt->createStatus().stop);

  etkt->loop();

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::TAG, status.stopped.command);
  TEST_ASSERT_EQUAL_INT((int)PendingStop::NONE, (int)status.stop);
}

// A run that is going fine and is longer than it needs to be. The label
// being pressed is finished and cut, and nothing more is fed. It is a
// finish, not a stop: nothing was cut short, so there is nothing for the
// panel to explain.
void test_a_run_stopped_after_a_label_finishes_that_label_only(void) {
  submitTag("AB", 3);
  stubAfterDelay() = [] {
    if (etkt->createStatus().copy == 1) {
      etkt->stopAfterLabel();
    }
  };

  etkt->loop();

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_UINT32(7, status.roll.feedsUsed);
  TEST_ASSERT_EQUAL_INT(1, display->last(DisplayCall::RENDER_PROGRESS)->copy);
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.stopped.command);
  TEST_ASSERT_EQUAL_INT((int)Screen::FINISHED, (int)display->screens().back());
  TEST_ASSERT_FALSE(display->last(DisplayCall::RENDER_IDLE)->stopped);
}

// A stop that arrives once the last label is cut is too late to cut
// anything short, but the operator still asked for the machine to stop. It
// ends the four seconds of blinking that say the run is done, and the
// panel is told the run finished.
void test_a_stop_during_the_finish_ends_the_celebration(void) {
  submitTag("AB", 1);
  stubAfterDelay() = [] {
    if (display->countOf(DisplayCall::RENDER) > 0) {
      etkt->stop();
    }
  };

  etkt->loop();

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.stopped.command);
  const DisplayCall* idle = display->last(DisplayCall::RENDER_IDLE);
  TEST_ASSERT_FALSE(idle->stopped);
  // The whole celebration takes 4.2 s.
  unsigned long finishedAt = 0;
  for (size_t i = 0; i < display->calls.size(); i++) {
    if (display->calls[i].kind == DisplayCall::RENDER) {
      finishedAt = display->calls[i].atMs;
    }
  }
  TEST_ASSERT_LESS_THAN_UINT32(500, idle->atMs - finishedAt);
}

// The panel goes on saying a job was stopped until a new job is accepted,
// so a phone that was not watching still hears about it -- but only until
// then.
void test_the_next_job_clears_the_last_stop(void) {
  submitTag("AB", 3);
  etkt->stop();
  etkt->loop();
  TEST_ASSERT_EQUAL_INT(Command::TAG, etkt->createStatus().stopped.command);

  submit(Command::FEED);

  TEST_ASSERT_EQUAL_INT(Command::IDLE, etkt->createStatus().stopped.command);
  etkt->loop();
}

// --- printing the last run again -------------------------------------------

// The button on the machine prints the last run again, with no phone and no
// network to ask it over: the same label, as many of it, cut or not.
void test_the_last_run_can_be_printed_again(void) {
  etkt->submit(tagOptions("AB", 2, false));
  etkt->loop();
  strokes->strokes.clear();

  TEST_ASSERT_TRUE(etkt->repeat());
  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::TAG, status.currentCommand);
  TEST_ASSERT_EQUAL_STRING("AB", status.currentLabel.c_str());
  TEST_ASSERT_EQUAL_INT(2, status.copies);
  etkt->loop();

  // A and B, twice, and nothing at the cut mark, on as much tape again.
  TEST_ASSERT_EQUAL_INT(4, (int)strokes->strokes.size());
  TEST_ASSERT_EQUAL_UINT32(28, etkt->createStatus().roll.feedsUsed);
  TEST_ASSERT_EQUAL_INT(Command::IDLE, etkt->createStatus().stopped.command);
}

// The machine is switched off between sessions, and the run to print again
// in the morning is the one printed the night before. Symbols and all: a
// label of them is three bytes a character where it is kept.
void test_the_last_run_outlives_a_reboot(void) {
  etkt->submit(tagOptions("♡ €5.00 ☆", 3, false));
  etkt->loop();

  reboot();

  TEST_ASSERT_TRUE(etkt->repeat());
  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::TAG, status.currentCommand);
  TEST_ASSERT_EQUAL_STRING("♡ €5.00 ☆", status.currentLabel.c_str());
  TEST_ASSERT_EQUAL_INT(3, status.copies);
  etkt->loop();
  // Seven characters to press, three times, and still not cut.
  TEST_ASSERT_EQUAL_INT(21, (int)strokes->strokes.size());
}

// A machine that has printed no run has none to print again, and says so:
// the button then does nothing, rather than pressing a label of nothing.
// Jobs that are not runs of labels leave nothing to repeat either.
void test_a_machine_that_has_printed_no_run_has_none_to_repeat(void) {
  submit(Command::FEED);
  etkt->loop();

  TEST_ASSERT_FALSE(etkt->repeat());

  TEST_ASSERT_EQUAL_INT(Command::IDLE, etkt->createStatus().currentCommand);
}

// The last run is a job like any other, and one at a time: asked for while
// another job has the machine, it is refused as a second tap would be.
void test_the_last_run_is_refused_while_another_job_has_the_machine(void) {
  submitTag("AB", 1);
  etkt->loop();
  submit(Command::FEED);

  TEST_ASSERT_TRUE(repeatRefused());

  TEST_ASSERT_EQUAL_INT(Command::FEED, etkt->createStatus().currentCommand);
  etkt->loop();
}

// A run stopped partway is still the last run, and all of it. The operator
// who stopped it to change the roll wants the run they asked for, not what
// happened to be left of it.
void test_a_stopped_run_is_still_the_last_run_as_it_was_asked_for(void) {
  display->onCall = [](const DisplayCall& call) {
    if (call.kind == DisplayCall::RENDER_PROGRESS && call.copy == 2) {
      etkt->stop();
    }
  };
  submitTag("AB", 3);
  etkt->loop();
  display->onCall = nullptr;
  TEST_ASSERT_EQUAL_INT(1, etkt->createStatus().stopped.printed);

  TEST_ASSERT_TRUE(etkt->repeat());

  TEST_ASSERT_EQUAL_INT(3, etkt->createStatus().copies);
  etkt->loop();
  TEST_ASSERT_EQUAL_INT(Command::IDLE, etkt->createStatus().stopped.command);
}

// The run printed again is the newest one, whatever was printed before it.
void test_a_new_run_takes_the_place_of_the_last_one(void) {
  submitTag("AB", 2);
  etkt->loop();
  etkt->submit(tagOptions("C", 1, false));
  etkt->loop();
  strokes->strokes.clear();

  TEST_ASSERT_TRUE(etkt->repeat());

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_STRING("C", status.currentLabel.c_str());
  TEST_ASSERT_EQUAL_INT(1, status.copies);
  etkt->loop();
  TEST_ASSERT_EQUAL_INT(1, (int)strokes->strokes.size());
}

// What is read back from the flash is started with nobody having typed it,
// so a stored run that is only partly a run is none at all: the machine
// does not guess at the rest of it.
void test_a_stored_run_that_cannot_be_read_is_not_printed(void) {
  const char* const unreadable[] = {
      "",
      "AB",
      "{\"label\":\"AB\"}",
      "{\"label\":\"AB\",\"copies\":0,\"cut\":true}",
      "{\"label\":\"AB\",\"copies\":501,\"cut\":true}",
      "{\"label\":\"AB\",\"copies\":\"2\",\"cut\":true}",
      "{\"label\":\"AB\",\"copies\":2,\"cut\":\"yes\"}",
      "{\"label\":7,\"copies\":2,\"cut\":true}",
      "{\"label\":\"AB\",\"copies\":2,\"cut\":tr",
  };
  for (const char* text : unreadable) {
    stubNvsText()["lastrun"]["run"] = text;
    reboot();

    TEST_ASSERT_FALSE_MESSAGE(etkt->repeat(), text);
    TEST_ASSERT_EQUAL_INT_MESSAGE(Command::IDLE,
                                  etkt->createStatus().currentCommand, text);
  }

  // And one that is whole is read, so it is the text above that is refused.
  stubNvsText()["lastrun"]["run"] =
      "{\"label\":\"AB\",\"copies\":2,\"cut\":true}";
  reboot();
  TEST_ASSERT_TRUE(etkt->repeat());
  etkt->loop();
}

// --- changing the roll at the machine --------------------------------------
// The roll runs out about every seventy labels, which makes the change the
// thing done most often at the machine. So the machine keeps track of whether
// its roll is out, through a reboot, and asks for the next one on its own
// screen, where the button can answer it with no phone and no network.

// A roll that has been unloaded is out, and where the idle screen would be
// the machine says what it wants next: a new roll, and the button.
void test_an_unloaded_roll_is_out_and_the_machine_asks_for_the_next(void) {
  submit(Command::REEL);
  etkt->loop();
  TEST_ASSERT_FALSE(etkt->createStatus().roll.out);
  display->clear();

  submit(Command::UNLOAD);
  etkt->loop();

  TEST_ASSERT_TRUE(etkt->createStatus().roll.out);
  TEST_ASSERT_EQUAL_INT((int)Screen::NEW_ROLL, (int)display->screens().back());
  TEST_ASSERT_EQUAL_INT(0, display->countOf(DisplayCall::RENDER_IDLE));
}

// An unload stopped partway has left the end of the tape somewhere short of
// the cutter, and cannot say whether it is still in the cog. A run printed
// from there would start on no tape, so the roll counts as out, and a load
// is what puts it right, whichever it was.
void test_an_unload_stopped_partway_takes_the_roll_out_all_the_same(void) {
  submit(Command::REEL);
  etkt->loop();
  static long threaded;
  threaded = feedStepper->currentPosition();
  feedStepper->afterStep = [] {
    if (feedStepper->currentPosition() != threaded) {
      etkt->stop();
    }
  };

  submit(Command::UNLOAD);
  etkt->loop();

  TEST_ASSERT_EQUAL_INT(Command::UNLOAD,
                        (int)etkt->createStatus().stopped.command);
  TEST_ASSERT_TRUE(etkt->createStatus().roll.out);
  TEST_ASSERT_EQUAL_INT((int)Screen::NEW_ROLL, (int)display->screens().back());
}

// A stop that is ahead of the unload leaves the tape at the cutter, where it
// was, and the roll in: the machine goes back to its idle screen, and the
// next run prints on it.
void test_an_unload_stopped_before_the_tape_moves_leaves_the_roll_in(void) {
  submit(Command::REEL);
  etkt->loop();
  const long threaded = feedStepper->currentPosition();

  submit(Command::UNLOAD);
  etkt->stop();
  etkt->loop();

  TEST_ASSERT_EQUAL_INT32(threaded, feedStepper->currentPosition());
  TEST_ASSERT_FALSE(etkt->createStatus().roll.out);
  TEST_ASSERT_TRUE(display->last(DisplayCall::RENDER_IDLE)->stopped);
}

// A load that runs to its end has threaded the new roll through to the
// cutter, and the idle screen comes back.
void test_a_load_that_runs_to_its_end_puts_the_roll_back_in(void) {
  submit(Command::UNLOAD);
  etkt->loop();
  TEST_ASSERT_TRUE(etkt->createStatus().roll.out);
  display->clear();

  submit(Command::REEL);
  etkt->loop();

  TEST_ASSERT_FALSE(etkt->createStatus().roll.out);
  TEST_ASSERT_EQUAL_INT((int)Screen::REELING, (int)display->screens().back());
  TEST_ASSERT_EQUAL_INT(1, display->countOf(DisplayCall::RENDER_IDLE));
}

// A load is stopped because the tape is not catching. The roll is no more
// threaded than it was, so it stays out and the machine goes on asking: the
// next press of the button loads it again, rather than printing on it.
void test_a_load_stopped_partway_leaves_the_roll_out(void) {
  submit(Command::UNLOAD);
  etkt->loop();
  static long backedOut;
  backedOut = feedStepper->currentPosition();
  feedStepper->afterStep = [] {
    if (feedStepper->currentPosition() != backedOut) {
      etkt->stop();
    }
  };

  submit(Command::REEL);
  etkt->loop();

  TEST_ASSERT_EQUAL_INT(Command::REEL,
                        (int)etkt->createStatus().stopped.command);
  TEST_ASSERT_TRUE(etkt->createStatus().roll.out);
  TEST_ASSERT_EQUAL_INT((int)Screen::NEW_ROLL, (int)display->screens().back());
}

// The machine is switched off with its roll out as easily as with one in,
// and says so again when it comes back up: what it shows once it has booted
// is the notice, not the idle screen.
void test_a_roll_that_is_out_is_still_out_after_a_reboot(void) {
  submit(Command::UNLOAD);
  etkt->loop();

  reboot();

  TEST_ASSERT_TRUE(etkt->createStatus().roll.out);
  etkt->showIdle();
  TEST_ASSERT_EQUAL_INT((int)Screen::NEW_ROLL, (int)display->screens().back());
  TEST_ASSERT_EQUAL_INT(0, display->countOf(DisplayCall::RENDER_IDLE));
}

// A machine that has never kept track takes its roll to be in, as every
// machine built before this did, and shows the idle screen once it has
// booted.
void test_a_machine_with_its_roll_in_shows_the_idle_screen(void) {
  TEST_ASSERT_FALSE(etkt->createStatus().roll.out);

  etkt->showIdle();

  TEST_ASSERT_EQUAL_INT(1, display->countOf(DisplayCall::RENDER_IDLE));
  TEST_ASSERT_FALSE(display->last(DisplayCall::RENDER_IDLE)->stopped);
  TEST_ASSERT_EQUAL_INT(0, (int)display->screens().size());
}

// --- the idle screen and the network ---------------------------------------

static ConnectionInfo onTheChurchNetwork(void) {
  ConnectionInfo info;
  info.name = "Church";
  info.detail = "192.168.1.50";
  info.qr = "http://192.168.1.50";
  return info;
}

// How the machine is reached changes with no job running: a network joined
// a minute after the boot, another address, the machine's own network
// opening. The idle screen is the one place that says it, and the job runner
// is the one task that draws, so it draws the idle screen again when it
// wakes to no job and a change. Once for a change, not once for every wake.
void test_a_waiting_machine_shows_a_change_in_how_it_is_reached(void) {
  etkt->showIdle();
  display->clear();
  display->setConnectionInfo(onTheChurchNetwork());

  etkt->loop();
  TEST_ASSERT_EQUAL_INT(1, display->countOf(DisplayCall::RENDER_IDLE));

  etkt->loop();
  TEST_ASSERT_EQUAL_INT(1, display->countOf(DisplayCall::RENDER_IDLE));
}

// The screen goes on saying the last job was stopped until the next job.
void test_the_idle_screen_drawn_again_still_says_the_job_was_stopped(void) {
  feedStepper->afterStep = [] { etkt->stop(); };
  submit(Command::FEED);
  etkt->loop();
  feedStepper->afterStep = nullptr;
  TEST_ASSERT_TRUE(display->last(DisplayCall::RENDER_IDLE)->stopped);
  display->clear();
  display->setConnectionInfo(onTheChurchNetwork());

  etkt->loop();

  TEST_ASSERT_EQUAL_INT(1, display->countOf(DisplayCall::RENDER_IDLE));
  TEST_ASSERT_TRUE(display->last(DisplayCall::RENDER_IDLE)->stopped);
}

// While the roll is out the machine goes on asking for the next one. The
// idle screen that follows the load says how the machine is reached by then.
void test_a_change_in_how_it_is_reached_leaves_the_roll_notice_up(void) {
  submit(Command::UNLOAD);
  etkt->loop();
  display->setConnectionInfo(onTheChurchNetwork());

  etkt->loop();

  TEST_ASSERT_EQUAL_INT((int)Screen::NEW_ROLL, (int)display->screens().back());
  TEST_ASSERT_EQUAL_INT(0, display->countOf(DisplayCall::RENDER_IDLE));
}

// --- how long it takes ---------------------------------------------------

// Where the estimate counts a job's home from: see Printhead::homeUs().
static void parkAtTheJ(void) {
  CommandOptions options;
  options.command = Command::MOVE;
  options.label = "J";
  etkt->submit(options);
  etkt->loop();
}

// A status a run reported, and when.
struct TimedStatus {
  unsigned long atMs;
  StatusUpdate status;
};

// A run as the machine printed it, by the virtual clock from the moment
// loop() took it: when each label began, from what the run reported, what
// it reported a tenth of a second or more apart, and when the machine went
// idle.
struct TimedRun {
  std::vector<unsigned long> labelStartMs;
  std::vector<TimedStatus> statuses;
  unsigned long runMs;
};

static TimedRun timeRun(const CommandOptions& options) {
  static TimedRun timed;
  static unsigned long startMs;
  timed = TimedRun();
  startMs = millis();
  stubAfterTick() = [] {
    const StatusUpdate status = etkt->createStatus();
    const unsigned long atMs = millis() - startMs;
    if (status.copy > (int)timed.labelStartMs.size()) {
      timed.labelStartMs.push_back(atMs);
    }
    if (status.currentCommand == Command::TAG &&
        (timed.statuses.empty() || atMs - timed.statuses.back().atMs >= 100)) {
      timed.statuses.push_back({atMs, status});
    }
  };
  etkt->submit(options);
  etkt->loop();
  stubAfterTick() = nullptr;
  timed.runMs = millis() - startMs;
  return timed;
}

// What the panel shows before a run is sent: how long a label takes, and
// the whole run. Worked out, not timed, and within a hundredth of the run
// as the machine prints it. A label is start to start once the run is
// under way, since the first comes after the tune and the home.
void test_the_estimate_of_a_run_is_how_long_it_takes(void) {
  const char* labels[] = {" HELLO ", "♡ €5.00 ☆"};
  for (const char* label : labels) {
    for (const bool cut : {true, false}) {
      parkAtTheJ();
      const CommandOptions options = tagOptions(label, 3, cut);

      const RunEstimate estimate = etkt->estimate(options);
      const TimedRun timed = timeRun(options);

      const String name = String(label) + (cut ? ", cut" : ", not cut");
      TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)timed.labelStartMs.size(),
                                    name.c_str());
      const unsigned long labelMs =
          timed.labelStartMs[2] - timed.labelStartMs[1];
      TEST_ASSERT_UINT32_WITHIN_MESSAGE(labelMs / 100, labelMs,
                                        estimate.labelMs, name.c_str());
      TEST_ASSERT_UINT32_WITHIN_MESSAGE(timed.runMs / 100, timed.runMs,
                                        estimate.runMs, name.c_str());
    }
  }
}

// While a run prints, the panel shows how long the run has left, the
// finish included. Every status the run reports is within a hundredth of
// the run of what was in fact left.
void test_a_run_reports_how_long_it_has_left(void) {
  const CommandOptions options = tagOptions(" HELLO ", 3, true);

  const TimedRun timed = timeRun(options);

  TEST_ASSERT_GREATER_THAN_INT(400, (int)timed.statuses.size());
  for (const TimedStatus& timedStatus : timed.statuses) {
    TEST_ASSERT_UINT32_WITHIN(timed.runMs / 100, timed.runMs - timedStatus.atMs,
                              timedStatus.status.remainingMs);
  }
  const StatusUpdate idle = etkt->createStatus();
  TEST_ASSERT_EQUAL_UINT32(0, idle.labelMs);
  TEST_ASSERT_EQUAL_UINT32(0, idle.remainingMs);
}

// The estimate leaves out what the board adds to every label, such as the
// OLED taking a few tens of milliseconds to draw each frame. From its third
// label on, a run reports the label time it measured over the labels before,
// and counts down from that.
void test_a_run_counts_down_from_the_label_time_it_measures(void) {
  display->onCall = [](const DisplayCall& call) {
    if (call.kind == DisplayCall::RENDER_PROGRESS) {
      delay(30);
    }
  };
  const CommandOptions options = tagOptions(" HELLO ", 5, true);
  const RunEstimate estimate = etkt->estimate(options);

  const TimedRun timed = timeRun(options);

  TEST_ASSERT_EQUAL(5, timed.labelStartMs.size());
  const unsigned long labelMs = timed.labelStartMs[2] - timed.labelStartMs[1];
  // Seven characters and the start of the label, each a frame.
  TEST_ASSERT_UINT32_WITHIN(estimate.labelMs / 100, estimate.labelMs + 8 * 30,
                            labelMs);
  for (const TimedStatus& timedStatus : timed.statuses) {
    const StatusUpdate& status = timedStatus.status;
    if (status.copy < 3) {
      TEST_ASSERT_EQUAL_UINT32(estimate.labelMs, status.labelMs);
      continue;
    }
    TEST_ASSERT_UINT32_WITHIN(1, labelMs, status.labelMs);
    TEST_ASSERT_UINT32_WITHIN(20, timed.runMs - timedStatus.atMs,
                              status.remainingMs);
  }
}

// Asked to stop after its label, a run counts down to the end of that label
// and the celebration, not to the end of every label it was sent for.
void test_a_run_stopping_after_its_label_counts_down_to_that_label(void) {
  for (int askedDuring = 1; askedDuring <= 2; askedDuring++) {
    parkAtTheJ();
    display->onCall = [askedDuring](const DisplayCall& call) {
      if (call.kind == DisplayCall::RENDER_PROGRESS &&
          call.copy == askedDuring && call.charactersDone == 3) {
        etkt->stopAfterLabel();
      }
    };

    const TimedRun timed = timeRun(tagOptions(" HELLO ", 5, true));

    TEST_ASSERT_EQUAL(askedDuring, timed.labelStartMs.size());
    int asked = 0;
    for (const TimedStatus& timedStatus : timed.statuses) {
      if (timedStatus.status.stop != PendingStop::AFTER_LABEL) {
        continue;
      }
      asked++;
      TEST_ASSERT_UINT32_WITHIN(timed.runMs / 100,
                                timed.runMs - timedStatus.atMs,
                                timedStatus.status.remainingMs);
    }
    TEST_ASSERT_GREATER_THAN_INT(50, asked);
  }
}

// A run stopping now has nothing left to count down but the press finishing
// its stroke.
void test_a_run_stopping_now_has_no_time_left(void) {
  static StatusUpdate stopping;
  display->onCall = [](const DisplayCall& call) {
    if (call.kind == DisplayCall::RENDER_PROGRESS && call.copy == 2 &&
        call.charactersDone == 3) {
      etkt->stop();
      stopping = etkt->createStatus();
    }
  };
  etkt->submit(tagOptions(" HELLO ", 5, true));

  etkt->loop();

  TEST_ASSERT_EQUAL(PendingStop::NOW, stopping.stop);
  TEST_ASSERT_EQUAL_UINT32(0, stopping.remainingMs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_submitted_feed_runs_and_the_machine_goes_idle);
  RUN_TEST(test_the_machine_is_busy_from_taking_a_job_until_the_job_ends);
  RUN_TEST(test_a_finished_run_shows_finished_and_records_no_stop);
  RUN_TEST(test_a_run_reports_which_label_it_is_on_and_how_far_into_it);
  RUN_TEST(test_the_wheel_turns_while_the_tape_feeds_up_to_each_character);
  RUN_TEST(test_a_run_can_leave_out_the_cut);
  RUN_TEST(test_the_cut_button_presses_three_times_and_a_label_once);
  RUN_TEST(test_a_reel_loads_a_roll_of_the_declared_length);
  RUN_TEST(test_a_reel_without_a_length_takes_the_last_roll_length);
  RUN_TEST(test_an_unload_backs_the_tape_out_as_far_as_a_reel_threads_it);
  RUN_TEST(test_an_unload_leaves_the_roll_count_as_it_was);
  RUN_TEST(test_a_stopped_unload_lets_go_of_the_tape_and_leaves_nothing_to_cut);
  RUN_TEST(test_saving_stores_the_calibration_and_reboots);
  RUN_TEST(test_a_second_job_is_refused_while_the_first_waits_its_turn);
  RUN_TEST(test_a_job_posted_while_the_idle_screen_draws_is_taken);
  RUN_TEST(test_no_job_starts_the_screen_again);
  RUN_TEST(test_the_full_test_cuts_at_the_align_it_is_testing);
  RUN_TEST(test_a_stop_with_nothing_running_stops_nothing_later);
  RUN_TEST(test_saving_cannot_be_stopped);
  RUN_TEST(test_every_command_but_saving_can_be_stopped);
  RUN_TEST(test_only_a_run_of_labels_can_stop_after_a_label);
  RUN_TEST(test_a_run_stopped_partway_through_a_label_leaves_it_on_the_tape);
  RUN_TEST(test_only_a_command_that_presses_a_label_leaves_one_unfinished);
  RUN_TEST(test_a_lost_wheel_ends_the_run_and_says_why);
  RUN_TEST(test_each_stop_is_told_apart_from_the_last);
  RUN_TEST(test_a_stop_now_overtakes_a_stop_after_the_label);
  RUN_TEST(test_a_run_stopped_after_a_label_finishes_that_label_only);
  RUN_TEST(test_a_stop_during_the_finish_ends_the_celebration);
  RUN_TEST(test_the_next_job_clears_the_last_stop);
  RUN_TEST(test_the_last_run_can_be_printed_again);
  RUN_TEST(test_the_last_run_outlives_a_reboot);
  RUN_TEST(test_a_machine_that_has_printed_no_run_has_none_to_repeat);
  RUN_TEST(test_the_last_run_is_refused_while_another_job_has_the_machine);
  RUN_TEST(test_a_stopped_run_is_still_the_last_run_as_it_was_asked_for);
  RUN_TEST(test_a_new_run_takes_the_place_of_the_last_one);
  RUN_TEST(test_a_stored_run_that_cannot_be_read_is_not_printed);
  RUN_TEST(test_an_unloaded_roll_is_out_and_the_machine_asks_for_the_next);
  RUN_TEST(test_an_unload_stopped_partway_takes_the_roll_out_all_the_same);
  RUN_TEST(test_an_unload_stopped_before_the_tape_moves_leaves_the_roll_in);
  RUN_TEST(test_a_load_that_runs_to_its_end_puts_the_roll_back_in);
  RUN_TEST(test_a_load_stopped_partway_leaves_the_roll_out);
  RUN_TEST(test_a_roll_that_is_out_is_still_out_after_a_reboot);
  RUN_TEST(test_a_machine_with_its_roll_in_shows_the_idle_screen);
  RUN_TEST(test_a_waiting_machine_shows_a_change_in_how_it_is_reached);
  RUN_TEST(test_the_idle_screen_drawn_again_still_says_the_job_was_stopped);
  RUN_TEST(test_a_change_in_how_it_is_reached_leaves_the_roll_notice_up);
  RUN_TEST(test_the_estimate_of_a_run_is_how_long_it_takes);
  RUN_TEST(test_a_run_reports_how_long_it_has_left);
  RUN_TEST(test_a_run_counts_down_from_the_label_time_it_measures);
  RUN_TEST(test_a_run_stopping_after_its_label_counts_down_to_that_label);
  RUN_TEST(test_a_run_stopping_now_has_no_time_left);
  return UNITY_END();
}
