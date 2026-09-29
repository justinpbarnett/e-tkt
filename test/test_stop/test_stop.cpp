// Host-side tests for StopSignal and the stoppable motion in Motion.h.
//
// A stop is raised on one task and obeyed on another, while a motor turns,
// and none of that is anything a person can watch closely enough to check.
// What these pin down is the part that decides what the operator is told
// afterwards: whether the job was cut short, or had already finished when the
// stop came. Run with:  pio test -e native
#include <unity.h>

#include "Arduino.h"
#include "FakeDrivers.h"
#include "Motion.h"
#include "StopSignal.h"

static FakeStepper* stepper;
static StopSignal* stop;

static long stepsTaken;
static long stopAtStep;

static void countStep() {
  stepsTaken++;
  if (stepsTaken == stopAtStep) {
    stop->raise();
  }
}

void setUp(void) {
  stubReset();
  stepper = new FakeStepper();
  stop = new StopSignal();
  stepsTaken = 0;
  stopAtStep = 0;
  stepper->afterStep = countStep;
}

void tearDown(void) {
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
  stop->raise();
  TEST_ASSERT_TRUE(stop->raised());
  TEST_ASSERT_FALSE(stop->cutShort());
}

void test_obeying_a_stop_is_remembered(void) {
  stop->raise();
  TEST_ASSERT_TRUE(stop->shouldStop());
  TEST_ASSERT_TRUE(stop->cutShort());
}

void test_clearing_forgets_both(void) {
  stop->raise();
  stop->shouldStop();
  stop->clear();
  TEST_ASSERT_FALSE(stop->raised());
  TEST_ASSERT_FALSE(stop->cutShort());
  TEST_ASSERT_FALSE(stop->shouldStop());
  TEST_ASSERT_FALSE(stop->cutShort());
}

// --- moving --------------------------------------------------------------

void test_a_move_nobody_stops_arrives(void) {
  TEST_ASSERT_TRUE(runToNewPosition(stepper, 42, stop));
  TEST_ASSERT_EQUAL_INT32(42, stepper->currentPosition());
  TEST_ASSERT_EQUAL_INT32(42, stepsTaken);
  TEST_ASSERT_FALSE(stop->cutShort());
}

void test_a_move_goes_to_a_position_rather_than_by_a_distance(void) {
  runToNewPosition(stepper, 10, stop);
  TEST_ASSERT_TRUE(runToNewPosition(stepper, -5, stop));
  TEST_ASSERT_EQUAL_INT32(-5, stepper->currentPosition());
  TEST_ASSERT_EQUAL_INT32(25, stepsTaken);
}

void test_a_move_to_where_the_motor_is_takes_no_steps(void) {
  runToNewPosition(stepper, 7, stop);
  stepsTaken = 0;
  TEST_ASSERT_TRUE(runToNewPosition(stepper, 7, stop));
  TEST_ASSERT_EQUAL_INT32(0, stepsTaken);
}

void test_a_stop_already_up_takes_no_steps(void) {
  stop->raise();
  TEST_ASSERT_FALSE(runToNewPosition(stepper, 42, stop));
  TEST_ASSERT_EQUAL_INT32(0, stepsTaken);
  TEST_ASSERT_EQUAL_INT32(0, stepper->currentPosition());
  TEST_ASSERT_TRUE(stop->cutShort());
}

void test_a_stop_halts_the_motor_within_a_step(void) {
  stopAtStep = 10;
  TEST_ASSERT_FALSE(runToNewPosition(stepper, 42, stop));
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
  TEST_ASSERT_TRUE(runToNewPosition(stepper, 42, stop));
  TEST_ASSERT_EQUAL_INT32(42, stepper->currentPosition());
  TEST_ASSERT_FALSE(stop->cutShort());
}

void test_a_halted_motor_is_left_holding(void) {
  // Letting go is the caller's decision. The wheel is left holding while the
  // command decides what comes next, and dropped along with everything else
  // when the command is over.
  stepper->enableOutputs();
  stopAtStep = 3;
  runToNewPosition(stepper, 42, stop);
  TEST_ASSERT_TRUE(stepper->energized);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_new_signal_is_down);
  RUN_TEST(test_looking_at_a_stop_is_not_obeying_it);
  RUN_TEST(test_obeying_a_stop_is_remembered);
  RUN_TEST(test_clearing_forgets_both);
  RUN_TEST(test_a_move_nobody_stops_arrives);
  RUN_TEST(test_a_move_goes_to_a_position_rather_than_by_a_distance);
  RUN_TEST(test_a_move_to_where_the_motor_is_takes_no_steps);
  RUN_TEST(test_a_stop_already_up_takes_no_steps);
  RUN_TEST(test_a_stop_halts_the_motor_within_a_step);
  RUN_TEST(test_a_stop_on_the_last_step_is_too_late_to_cut_anything_short);
  RUN_TEST(test_a_halted_motor_is_left_holding);
  return UNITY_END();
}
