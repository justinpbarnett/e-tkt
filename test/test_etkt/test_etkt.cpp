// Host-side tests for the job runner, ETKT, through the four calls the rest
// of the firmware makes on it: submit, stop, createStatus and loop.
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

void setUp(void) {
  stubReset();
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

void tearDown(void) { delete machine; }

static void submit(Command command) {
  CommandOptions options;
  options.command = command;
  etkt->submit(options);
}

static void submitTag(const String& label, int copies) {
  CommandOptions options;
  options.command = Command::TAG;
  options.label = label;
  options.copies = copies;
  etkt->submit(options);
}

static bool refused(Command command) {
  try {
    submit(command);
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
  // One feed is 4 mm off the roll, and the panel's tape gauge reads it here.
  TEST_ASSERT_EQUAL_UINT32(1, status.roll.feedsUsed);
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
  TEST_ASSERT_EQUAL_INT(7, saved->align);
  TEST_ASSERT_EQUAL_INT(3, saved->force);
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
  // After the lead feed, so the tape has moved: the wheel is on its way to
  // the first character.
  charStepper->afterStep = [] {
    if (feeder->feeds() > 0) {
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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_submitted_feed_runs_and_the_machine_goes_idle);
  RUN_TEST(test_a_finished_run_shows_finished_and_records_no_stop);
  RUN_TEST(test_a_run_reports_which_label_it_is_on_and_how_far_into_it);
  RUN_TEST(test_a_reel_loads_a_roll_of_the_declared_length);
  RUN_TEST(test_a_reel_without_a_length_takes_the_last_roll_length);
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
  return UNITY_END();
}
