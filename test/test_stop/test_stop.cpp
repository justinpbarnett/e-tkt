// Host-side tests for StopSignal and the stoppable motion in Motion.h.
//
// A stop is raised on one task and obeyed on another, while a motor turns,
// and none of that is anything a person can watch closely enough to check.
// What these pin down is the part that decides what the operator is told
// afterwards: whether the job was cut short, or had already finished when the
// stop came. Run with:  pio test -e native
#include <unity.h>

#include "Arduino.h"
#include "Configuration.h"
#include "FakeBackground.h"
#include "FakeDrivers.h"
#include "Motion.h"
#include "StopSignal.h"

static FakeStepper* stepper;
static StopSignal* stop;

// What runs while the motor turns, as the tape does while the wheel turns.
static FakeBackground* background;

static long stepsTaken;
static long stopAtStep;

static void countStep() {
  stepsTaken++;
  if (stepsTaken == stopAtStep) {
    stop->raise(StopCause::OPERATOR);
  }
}

void setUp(void) {
  stubReset();
  stepper = new FakeStepper();
  // The daisy wheel's, as DaisyWheel::initialize() sets them. Left at
  // AccelStepper's defaults, a step a second, a move of 42 steps would take
  // 42 seconds of the virtual clock.
  stepper->setMaxSpeed(CHARACTER_STEPPER_MAX_SPEED);
  stepper->setAcceleration(CHARACTER_STEPPER_MAX_ACCELERATION);
  stop = new StopSignal();
  background = new FakeBackground(stepper);
  stepsTaken = 0;
  stopAtStep = 0;
  stepper->afterStep = countStep;
}

void tearDown(void) {
  delete background;
  delete stop;
  delete stepper;
}

// --- the signal ----------------------------------------------------------

void test_a_new_signal_is_down(void) {
  TEST_ASSERT_FALSE(stop->raised());
  TEST_ASSERT_FALSE(stop->shouldStop());
  TEST_ASSERT_FALSE(stop->cutShort());
}

void test_looking_at_a_stop_is_not_obeying_it(void) {
  // The status report looks while a job runs. If looking counted, every stop
  // that landed after the last label was cut would read as cutting it short.
  stop->raise(StopCause::OPERATOR);
  TEST_ASSERT_TRUE(stop->raised());
  TEST_ASSERT_FALSE(stop->cutShort());
}

void test_obeying_a_stop_is_remembered(void) {
  stop->raise(StopCause::OPERATOR);
  TEST_ASSERT_TRUE(stop->shouldStop());
  TEST_ASSERT_TRUE(stop->cutShort());
}

void test_clearing_forgets_both(void) {
  stop->raise(StopCause::OPERATOR);
  stop->shouldStop();
  stop->clear();
  TEST_ASSERT_FALSE(stop->raised());
  TEST_ASSERT_FALSE(stop->cutShort());
  TEST_ASSERT_FALSE(stop->shouldStop());
  TEST_ASSERT_FALSE(stop->cutShort());
}

// The operator is told why the job ended, so a stop keeps what raised it.
void test_a_stop_says_what_raised_it(void) {
  TEST_ASSERT_TRUE(stop->cause() == StopCause::NONE);
  stop->raise(StopCause::LOST_WHEEL);
  TEST_ASSERT_TRUE(stop->raised());
  TEST_ASSERT_TRUE(stop->cause() == StopCause::LOST_WHEEL);
}

// A lost wheel stops the job, and the operator may press stop while the
// machine parks. What ended the job was the wheel, so the first cause stands.
void test_the_first_cause_of_a_stop_stands(void) {
  stop->raise(StopCause::LOST_WHEEL);
  stop->raise(StopCause::OPERATOR);
  TEST_ASSERT_TRUE(stop->cause() == StopCause::LOST_WHEEL);
}

// The next job starts with no stop, and no reason for one.
void test_clearing_forgets_the_cause(void) {
  stop->raise(StopCause::OPERATOR);
  stop->clear();
  TEST_ASSERT_TRUE(stop->cause() == StopCause::NONE);
  stop->raise(StopCause::LOST_WHEEL);
  TEST_ASSERT_TRUE(stop->cause() == StopCause::LOST_WHEEL);
}

// --- moving --------------------------------------------------------------

void test_a_move_nobody_stops_arrives(void) {
  TEST_ASSERT_TRUE(runToNewPosition(stepper, 42, stop, background));
  TEST_ASSERT_EQUAL_INT32(42, stepper->currentPosition());
  TEST_ASSERT_EQUAL_INT32(42, stepsTaken);
  TEST_ASSERT_FALSE(stop->cutShort());
}

void test_a_move_goes_to_a_position_rather_than_by_a_distance(void) {
  runToNewPosition(stepper, 10, stop, background);
  TEST_ASSERT_TRUE(runToNewPosition(stepper, -5, stop, background));
  TEST_ASSERT_EQUAL_INT32(-5, stepper->currentPosition());
  TEST_ASSERT_EQUAL_INT32(25, stepsTaken);
}

void test_a_move_to_where_the_motor_is_takes_no_steps(void) {
  runToNewPosition(stepper, 7, stop, background);
  stepsTaken = 0;
  TEST_ASSERT_TRUE(runToNewPosition(stepper, 7, stop, background));
  TEST_ASSERT_EQUAL_INT32(0, stepsTaken);
}

void test_a_stop_already_up_takes_no_steps(void) {
  stop->raise(StopCause::OPERATOR);
  TEST_ASSERT_FALSE(runToNewPosition(stepper, 42, stop, background));
  TEST_ASSERT_EQUAL_INT32(0, stepsTaken);
  TEST_ASSERT_EQUAL_INT32(0, stepper->currentPosition());
  TEST_ASSERT_TRUE(stop->cutShort());
}

void test_a_stop_halts_the_motor_within_a_step(void) {
  stopAtStep = 10;
  TEST_ASSERT_FALSE(runToNewPosition(stepper, 42, stop, background));
  TEST_ASSERT_EQUAL_INT32(10, stepsTaken);
  TEST_ASSERT_EQUAL_INT32(10, stepper->currentPosition());
  // Halted rather than slowed: where it is is now where it is going.
  TEST_ASSERT_EQUAL_INT32(0, stepper->distanceToGo());
  TEST_ASSERT_TRUE(stop->cutShort());
}

void test_a_stop_on_the_last_step_is_too_late_to_cut_anything_short(void) {
  // The motor arrived, the way AccelStepper reports it: run() says so on the
  // call that takes the last step, and the loop is over before it looks.
  stopAtStep = 42;
  TEST_ASSERT_TRUE(runToNewPosition(stepper, 42, stop, background));
  TEST_ASSERT_EQUAL_INT32(42, stepper->currentPosition());
  TEST_ASSERT_FALSE(stop->cutShort());
}

void test_a_halted_motor_is_left_holding(void) {
  // Letting go is the caller's decision. The wheel is left holding while the
  // command decides what comes next, and dropped along with everything else
  // when the command is over.
  stepper->enableOutputs();
  stopAtStep = 3;
  runToNewPosition(stepper, 42, stop, background);
  TEST_ASSERT_TRUE(stepper->energized);
}

// --- in the background ---------------------------------------------------
// Nothing turns a motor for you. Whatever runs beside a move gets its turns
// from the loop that makes it, so the move must never keep it waiting.

void test_a_move_keeps_the_background_going_as_it_turns(void) {
  const unsigned long start = micros();
  TEST_ASSERT_TRUE(runToNewPosition(stepper, 42, stop, background));
  bool partway = false;
  for (long position : background->positions) {
    partway = partway || (position > 0 && position < 42);
  }
  TEST_ASSERT_TRUE_MESSAGE(partway, "a turn partway, not only either end");
  TEST_ASSERT_LESS_THAN_UINT32(1000,
                               background->longestWaitUs(start, micros()));
}

void test_a_pause_keeps_the_background_going_for_as_long_as_it_lasts(void) {
  const unsigned long start = micros();
  pause(100, background);
  TEST_ASSERT_TRUE(micros() - start >= 100000UL);
  TEST_ASSERT_FALSE(background->atUs.empty());
  TEST_ASSERT_LESS_THAN_UINT32(1000,
                               background->longestWaitUs(start, micros()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_new_signal_is_down);
  RUN_TEST(test_looking_at_a_stop_is_not_obeying_it);
  RUN_TEST(test_obeying_a_stop_is_remembered);
  RUN_TEST(test_clearing_forgets_both);
  RUN_TEST(test_a_stop_says_what_raised_it);
  RUN_TEST(test_the_first_cause_of_a_stop_stands);
  RUN_TEST(test_clearing_forgets_the_cause);
  RUN_TEST(test_a_move_nobody_stops_arrives);
  RUN_TEST(test_a_move_goes_to_a_position_rather_than_by_a_distance);
  RUN_TEST(test_a_move_to_where_the_motor_is_takes_no_steps);
  RUN_TEST(test_a_stop_already_up_takes_no_steps);
  RUN_TEST(test_a_stop_halts_the_motor_within_a_step);
  RUN_TEST(test_a_stop_on_the_last_step_is_too_late_to_cut_anything_short);
  RUN_TEST(test_a_halted_motor_is_left_holding);
  RUN_TEST(test_a_move_keeps_the_background_going_as_it_turns);
  RUN_TEST(test_a_pause_keeps_the_background_going_for_as_long_as_it_lasts);
  return UNITY_END();
}
