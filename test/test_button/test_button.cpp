// Host-side tests for the button on the machine: what a press of it does,
// and what is not taken for a press.
//
// The button works the machine with no phone and no network: a press stops a
// job, a press with nothing running prints the last run again, and a hold
// and a press change the roll. None of that could be checked without a
// machine, a roll of tape and a finger. The machine here is the real one,
// module for module, with fakes where it meets the hardware: a HostMachine.
// The finger is the pin's reading by the clock, and the button is read as
// the board's task reads it, every BUTTON_POLL_MS, whatever the job runner is
// doing.
//
// Run with:  pio test -e native
#include <unity.h>

#include "Arduino.h"
#include "Button.h"
#include "Configuration.h"
#include "HostMachine.h"

static HostMachine* machine;
static ETKT* etkt;
static Button* button;

// When the finger is on the button, by the clock: from downAtMs until
// upAtMs.
static unsigned long downAtMs;
static unsigned long upAtMs;

static bool fingerDown(void) {
  const unsigned long now = millis();
  return now >= downAtMs && now < upAtMs;
}

// Puts the finger on the button `afterMs` from now, for `forMs`.
static void pressIn(unsigned long afterMs, unsigned long forMs) {
  downAtMs = millis() + afterMs;
  upAtMs = downAtMs + forMs;
}

// The button as the machine starts reading it.
static void startButton(void) {
  button = new Button(WIFI_RESET_PIN, etkt, &machine->logger);
  button->initialize();
}

// Lets the machine sit idle for long enough that a press is meant for an
// idle machine. See the presses that are not meant for it, below.
static void rest(void) { delay(BUTTON_ARMING_MS); }

void setUp(void) {
  stubReset();
  machine = new HostMachine();
  etkt = &machine->etkt;
  downAtMs = 0;
  upAtMs = 0;
  // The switch shorts the pin to ground, and the pin is pulled up otherwise.
  stubDigitalRead() = [](uint8_t pin) {
    return pin == WIFI_RESET_PIN && !fingerDown() ? HIGH : LOW;
  };
  button = NULL;
  // The board reads the button on a task of its own. Here it is read as the
  // clock passes each of the task's turns, which the waits and the motor
  // moves of a job pass too.
  stubAfterTick() = [] {
    if (button != NULL && millis() % BUTTON_POLL_MS == 0) {
      button->poll();
    }
  };
  startButton();
  rest();
}

void tearDown(void) {
  delete button;
  button = NULL;
  delete machine;
}

// A run of labels from the panel, start to finish.
static void run(const String& label, int copies) {
  etkt->submit(tagOptions(label, copies));
  etkt->loop();
}

// A press with nothing running: down, up, and long enough afterwards for the
// button to have read both.
static void press(void) {
  pressIn(0, 200);
  delay(200 + 2 * BUTTON_DEBOUNCE_MS);
}

// The button held down with nothing running, for as long as a hold takes
// and a second more. It returns with the finger still down.
static void hold(void) {
  pressIn(0, BUTTON_HOLD_MS + 1000);
  delay(BUTTON_HOLD_MS + 2 * BUTTON_DEBOUNCE_MS);
}

// --- stopping a job --------------------------------------------------------

void test_a_press_while_a_job_runs_stops_it(void) {
  // The stop the machine did not have: until this, a job could only be
  // stopped from a phone, over the network.
  etkt->submit(tagOptions("AB", 3));
  pressIn(3000, 200);

  etkt->loop();

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::TAG, status.stopped.command);
  TEST_ASSERT_TRUE(StopCause::OPERATOR == status.stopped.cause);
  TEST_ASSERT_LESS_THAN_INT(3, status.stopped.printed);
}

void test_the_press_that_stopped_a_job_starts_nothing_however_long_it_is_held(
    void) {
  // A finger that stays on the button after the stop is still stopping the
  // job, not asking for the roll to be unloaded or the run to start again.
  etkt->submit(tagOptions("AB", 3));
  pressIn(3000, 4000);

  etkt->loop();
  delay(5000);

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.currentCommand);
  TEST_ASSERT_FALSE(status.roll.out);
  // Still on record, which a job taken since would have cleared.
  TEST_ASSERT_EQUAL_INT(Command::TAG, status.stopped.command);
}

// --- printing the last run again -------------------------------------------

void test_a_press_while_nothing_runs_prints_the_last_run_again(void) {
  run("AB", 2);
  rest();

  press();

  const StatusUpdate taken = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::TAG, taken.currentCommand);
  TEST_ASSERT_EQUAL_STRING("AB", taken.currentLabel.c_str());
  TEST_ASSERT_EQUAL_INT(2, taken.copies);
  etkt->loop();
  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.currentCommand);
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.stopped.command);
  // Both runs, at seven feeds a label.
  TEST_ASSERT_EQUAL_UINT32(28, status.roll.feedsUsed);
}

void test_a_press_on_a_machine_that_has_printed_no_run_starts_nothing(void) {
  press();

  TEST_ASSERT_EQUAL_INT(Command::IDLE, etkt->createStatus().currentCommand);
}

// --- changing the roll -----------------------------------------------------

void test_holding_the_button_unloads_the_roll(void) {
  hold();

  TEST_ASSERT_EQUAL_INT(Command::UNLOAD, etkt->createStatus().currentCommand);
  // The finger is still down as the unload begins, and comes up during it.
  // Neither stops the unload the hold asked for.
  etkt->loop();
  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_TRUE(status.roll.out);
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.stopped.command);
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.currentCommand);
}

void test_a_hold_unloads_the_roll_once_however_long_it_lasts(void) {
  // A finger left on the button after the tape has backed out is not asking
  // for it to be backed out again.
  pressIn(0, 60000);
  delay(BUTTON_HOLD_MS + 2 * BUTTON_DEBOUNCE_MS);
  etkt->loop();

  delay(BUTTON_ARMING_MS + BUTTON_HOLD_MS + 1000);

  TEST_ASSERT_TRUE(fingerDown());
  TEST_ASSERT_EQUAL_INT(Command::IDLE, etkt->createStatus().currentCommand);
}

void test_a_roll_is_changed_and_the_run_printed_again_with_no_phone(void) {
  // The whole change, as it is done at the machine about every seventy
  // labels: hold to unload, put the new roll in, press to load it, and
  // press to print the run again.
  run("AB", 2);
  rest();
  hold();
  etkt->loop();
  rest();

  press();

  TEST_ASSERT_EQUAL_INT(Command::REEL, etkt->createStatus().currentCommand);
  etkt->loop();
  const StatusUpdate loaded = etkt->createStatus();
  TEST_ASSERT_FALSE(loaded.roll.out);
  // As long as the last roll, since nobody at the machine can say.
  TEST_ASSERT_EQUAL_INT(DEFAULT_ROLL_LENGTH_MM, loaded.roll.lengthMm);

  rest();
  press();

  const StatusUpdate taken = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::TAG, taken.currentCommand);
  TEST_ASSERT_EQUAL_STRING("AB", taken.currentLabel.c_str());
  TEST_ASSERT_EQUAL_INT(2, taken.copies);
}

// --- what is not a press, and presses not meant for an idle machine --------

void test_a_flicker_of_the_pin_is_not_a_press(void) {
  // Noise on the wire, or a contact bouncing: the pin low for less than
  // BUTTON_DEBOUNCE_MS. An idle machine starts nothing for it, and a job is
  // not stopped by it. A boot once read this pin low with nobody near it,
  // and the machine forgot its network.
  const unsigned long flicker = BUTTON_DEBOUNCE_MS - BUTTON_POLL_MS;
  run("AB", 1);
  rest();

  pressIn(0, flicker);
  delay(500);

  TEST_ASSERT_EQUAL_INT(Command::IDLE, etkt->createStatus().currentCommand);

  etkt->submit(tagOptions("AB", 1));
  pressIn(3000, flicker);
  etkt->loop();

  TEST_ASSERT_EQUAL_INT(Command::IDLE, etkt->createStatus().stopped.command);
}

void test_a_press_just_after_a_job_ended_starts_nothing(void) {
  // A finger on its way to stop a job, landing just after the job ended by
  // itself. Taken as a press on an idle machine it would start the run the
  // operator was trying to stop.
  run("AB", 2);

  press();

  TEST_ASSERT_EQUAL_INT(Command::IDLE, etkt->createStatus().currentCommand);

  // Once the machine has sat idle for a moment, a press is meant for it.
  rest();
  press();

  TEST_ASSERT_EQUAL_INT(Command::TAG, etkt->createStatus().currentCommand);
}

void test_a_button_already_down_as_the_machine_starts_starts_nothing(void) {
  // Held through the boot, which is how the saved network is cleared, or a
  // pin that reads low before its pull-up has it. Not a press, and not a
  // hold, however long it lasts.
  run("AB", 1);
  delete button;
  button = NULL;
  pressIn(0, 5000);
  startButton();

  delay(6000);

  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.currentCommand);
  TEST_ASSERT_FALSE(status.roll.out);

  // And it is an ordinary button from then on.
  press();

  TEST_ASSERT_EQUAL_INT(Command::TAG, etkt->createStatus().currentCommand);
}

void test_a_press_overtaken_by_a_job_from_the_panel_leaves_that_job_alone(
    void) {
  // The machine was idle as the finger came down, and the panel started a
  // job before it came up. The press started nothing, so it has nothing to
  // do with that job: the job is not stopped, and no run is put behind it.
  run("AB", 1);
  rest();
  pressIn(0, 300);
  delay(150);
  CommandOptions feed;
  feed.command = Command::FEED;
  etkt->submit(feed);

  delay(300);

  TEST_ASSERT_EQUAL_INT(Command::FEED, etkt->createStatus().currentCommand);
  etkt->loop();
  const StatusUpdate status = etkt->createStatus();
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.currentCommand);
  TEST_ASSERT_EQUAL_INT(Command::IDLE, status.stopped.command);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_press_while_a_job_runs_stops_it);
  RUN_TEST(
      test_the_press_that_stopped_a_job_starts_nothing_however_long_it_is_held);
  RUN_TEST(test_a_press_while_nothing_runs_prints_the_last_run_again);
  RUN_TEST(test_a_press_on_a_machine_that_has_printed_no_run_starts_nothing);
  RUN_TEST(test_holding_the_button_unloads_the_roll);
  RUN_TEST(test_a_hold_unloads_the_roll_once_however_long_it_lasts);
  RUN_TEST(test_a_roll_is_changed_and_the_run_printed_again_with_no_phone);
  RUN_TEST(test_a_flicker_of_the_pin_is_not_a_press);
  RUN_TEST(test_a_press_just_after_a_job_ended_starts_nothing);
  RUN_TEST(test_a_button_already_down_as_the_machine_starts_starts_nothing);
  RUN_TEST(
      test_a_press_overtaken_by_a_job_from_the_panel_leaves_that_job_alone);
  return UNITY_END();
}
