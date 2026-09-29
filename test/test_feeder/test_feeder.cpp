// Host-side tests for Feeder, driven through the StepperDriver seam.
//
// Feeder is small, but three of the things it does are easy to break and
// impossible to see: it must leave the motor de-energised after every feed,
// or the tape cannot be pulled and the driver cooks; it must feed relative to
// wherever the tape already is rather than to an absolute position; and it
// must let go of the tape the moment the operator stops it. A FakeStepper
// records the whole call sequence so all three are assertions. Run with:
// pio test -e native
#include <unity.h>

#include "Arduino.h"
#include "Configuration.h"
#include "FakeDrivers.h"
#include "Feeder.h"
#include "Logger.h"
#include "StopSignal.h"

// One feed is an eighth of a turn of the feed motor, in whichever direction
// the configuration says. Integer division, exactly as Feeder computes it.
static const long FEED_STEP = (FEED_MOTOR_STEPS_PER_REVOLUTION / 8);
static const long FEED_DIRECTION = REVERSE_FEED_STEPPER_DIRECTION ? 1 : -1;

static Logger* logger;
static FakeStepper* stepper;
static StopSignal* stop;
static Feeder* feeder;

// How many steps a move has taken since the test began, and the step to
// raise the stop on, where 0 is never.
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
  logger = new Logger();
  stepper = new FakeStepper();
  stop = new StopSignal();
  feeder = new Feeder(logger, stepper, stop);
  stepsTaken = 0;
  stopAtStep = 0;
  stepper->afterStep = countStep;
}

void tearDown(void) {
  delete feeder;
  delete stop;
  delete stepper;
  delete logger;
}

// --- startup -------------------------------------------------------------

void test_initialize_sets_speed_and_acceleration(void) {
  feeder->initialize();
  TEST_ASSERT_EQUAL_FLOAT(FEED_STEPPER_MAX_SPEED, stepper->maxSpeed);
  TEST_ASSERT_EQUAL_FLOAT(FEED_STEPPER_MAX_ACCELERATION, stepper->acceleration);
}

void test_initialize_leaves_the_motor_free(void) {
  // The tape is threaded by hand at power-on. A feeder holding current here
  // fights whoever is loading it.
  feeder->initialize();
  TEST_ASSERT_FALSE(stepper->energized);
  TEST_ASSERT_EQUAL_INT(0, stepper->countOf(StepperCall::ENABLE_OUTPUTS));
}

// --- feeding -------------------------------------------------------------

void test_one_feed_advances_one_eighth_of_a_revolution(void) {
  feeder->initialize();
  feeder->feed();
  const std::vector<long> moves = stepper->valuesOf(StepperCall::MOVE);
  TEST_ASSERT_EQUAL_INT(1, (int)moves.size());
  TEST_ASSERT_EQUAL_INT32(FEED_STEP * FEED_DIRECTION, moves[0]);
  // Asking is not arriving: the steps have to be run as well.
  TEST_ASSERT_EQUAL_INT32(FEED_STEP * FEED_DIRECTION,
                          stepper->currentPosition());
}

void test_a_repeated_feed_moves_once_per_repeat(void) {
  feeder->initialize();
  feeder->feed(3);
  TEST_ASSERT_EQUAL_INT(3, stepper->countOf(StepperCall::MOVE));
  TEST_ASSERT_EQUAL_INT32(FEED_STEP * FEED_DIRECTION * 3,
                          stepper->currentPosition());
}

void test_feeding_is_relative_to_where_the_tape_already_is(void) {
  // Two separate feeds must land six eighths along, not one eighth twice.
  feeder->initialize();
  feeder->feed(3);
  feeder->feed(3);
  TEST_ASSERT_EQUAL_INT(6, stepper->countOf(StepperCall::MOVE));
  TEST_ASSERT_EQUAL_INT32(FEED_STEP * FEED_DIRECTION * 6,
                          stepper->currentPosition());
}

void test_the_tape_only_ever_goes_one_way(void) {
  feeder->initialize();
  feeder->feed(5);
  const std::vector<long> moves = stepper->valuesOf(StepperCall::MOVE);
  TEST_ASSERT_EQUAL_INT(5, (int)moves.size());
  for (size_t i = 0; i < moves.size(); i++) {
    TEST_ASSERT_EQUAL_INT32(FEED_STEP * FEED_DIRECTION, moves[i]);
  }
}

void test_feed_energizes_before_moving_and_releases_after(void) {
  feeder->initialize();
  feeder->feed();
  const int enabled = stepper->firstIndexOf(StepperCall::ENABLE_OUTPUTS);
  const int moved = stepper->firstIndexOf(StepperCall::MOVE);
  const int arrived = stepper->lastIndexOf(StepperCall::RUN);
  const int disabled = stepper->firstIndexOf(StepperCall::DISABLE_OUTPUTS);
  TEST_ASSERT_TRUE(enabled >= 0 && moved >= 0 && arrived >= 0 && disabled >= 0);
  TEST_ASSERT_TRUE_MESSAGE(enabled < moved,
                           "the coils must be live before the motor is asked "
                           "to move");
  TEST_ASSERT_TRUE_MESSAGE(arrived < disabled,
                           "the coils must stay live until the move finishes");
}

void test_the_motor_is_never_left_energized(void) {
  feeder->initialize();
  feeder->feed(4);
  TEST_ASSERT_FALSE(stepper->energized);
}

void test_a_feed_of_nothing_still_releases_the_motor(void) {
  // Nothing calls feed(0) today, but leaving the coils live on the empty path
  // is the kind of thing that only shows up as a hot driver.
  feeder->initialize();
  feeder->feed(0);
  TEST_ASSERT_EQUAL_INT(0, stepper->countOf(StepperCall::MOVE));
  TEST_ASSERT_FALSE(stepper->energized);
}

void test_deenergize_releases_the_motor(void) {
  feeder->initialize();
  stepper->enableOutputs();
  feeder->deenergize();
  TEST_ASSERT_FALSE(stepper->energized);
}

// --- counting ------------------------------------------------------------
// The count is the only evidence of how much of the roll is gone, so it has
// to match what the motor did, move for move.

void test_nothing_is_counted_before_the_first_feed(void) {
  feeder->initialize();
  TEST_ASSERT_EQUAL_INT32(0, feeder->feeds());
}

void test_each_repeat_is_counted(void) {
  feeder->initialize();
  feeder->feed(3);
  TEST_ASSERT_EQUAL_INT32(3, feeder->feeds());
  TEST_ASSERT_EQUAL_INT32(stepper->countOf(StepperCall::MOVE), feeder->feeds());
}

void test_the_count_carries_across_feeds(void) {
  feeder->initialize();
  feeder->feed();
  feeder->feed(16);
  feeder->feed(2);
  TEST_ASSERT_EQUAL_INT32(19, feeder->feeds());
}

void test_a_feed_of_nothing_counts_nothing(void) {
  feeder->initialize();
  feeder->feed(0);
  TEST_ASSERT_EQUAL_INT32(0, feeder->feeds());
}

// --- timing --------------------------------------------------------------

void test_the_coils_are_given_time_to_come_up_before_the_first_move(void) {
  // The 10ms after enableOutputs() is there so the driver has settled before
  // the first step arrives.
  feeder->initialize();
  feeder->feed();
  const std::vector<StepperCall>& calls = stepper->calls;
  const int enabled = stepper->firstIndexOf(StepperCall::ENABLE_OUTPUTS);
  const int stepped = stepper->firstIndexOf(StepperCall::RUN);
  TEST_ASSERT_TRUE_MESSAGE(calls[stepped].atMs > calls[enabled].atMs,
                           "the first step must not land on the same tick as "
                           "the enable");
}

// --- stopping ------------------------------------------------------------
// The operator stops the machine because something has gone wrong with the
// tape, so the feeder is the part that most has to let go at once.

void test_a_stop_before_a_feed_leaves_the_motor_alone(void) {
  feeder->initialize();
  stop->raise(StopCause::OPERATOR);
  feeder->feed(3);
  TEST_ASSERT_EQUAL_INT(0, stepper->countOf(StepperCall::ENABLE_OUTPUTS));
  TEST_ASSERT_EQUAL_INT(0, stepper->countOf(StepperCall::MOVE));
  TEST_ASSERT_EQUAL_INT32(0, feeder->feeds());
  TEST_ASSERT_TRUE(stop->cutShort());
}

void test_a_stop_halts_the_feed_at_the_step_it_arrives_on(void) {
  feeder->initialize();
  stopAtStep = FEED_STEP / 2;
  feeder->feed(3);
  TEST_ASSERT_EQUAL_INT32(FEED_STEP / 2, stepsTaken);
  TEST_ASSERT_EQUAL_INT32(FEED_STEP / 2 * FEED_DIRECTION,
                          stepper->currentPosition());
  // Halted, rather than left to finish: nothing more to go.
  TEST_ASSERT_EQUAL_INT32(0, stepper->distanceToGo());
  TEST_ASSERT_TRUE(stop->cutShort());
}

void test_a_stop_skips_the_feeds_still_to_come(void) {
  feeder->initialize();
  stopAtStep = FEED_STEP + 1;
  feeder->feed(5);
  TEST_ASSERT_EQUAL_INT(2, stepper->countOf(StepperCall::MOVE));
  TEST_ASSERT_EQUAL_INT32(FEED_STEP + 1, stepsTaken);
}

void test_a_stopped_feed_still_releases_the_motor(void) {
  // Let go of the tape, so whoever is clearing the jam can pull it.
  feeder->initialize();
  stopAtStep = 1;
  feeder->feed(3);
  TEST_ASSERT_FALSE(stepper->energized);
}

void test_a_feed_a_stop_cut_short_counts_as_a_whole_one(void) {
  // Some tape went through, and the roll is better thought shorter than it
  // is than longer.
  feeder->initialize();
  stopAtStep = FEED_STEP + 1;
  feeder->feed(5);
  TEST_ASSERT_EQUAL_INT32(2, feeder->feeds());
}

void test_a_feed_that_finishes_before_a_stop_is_not_cut_short(void) {
  feeder->initialize();
  feeder->feed(2);
  stop->raise(StopCause::OPERATOR);
  TEST_ASSERT_EQUAL_INT32(2, feeder->feeds());
  TEST_ASSERT_FALSE(stop->cutShort());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_initialize_sets_speed_and_acceleration);
  RUN_TEST(test_initialize_leaves_the_motor_free);
  RUN_TEST(test_one_feed_advances_one_eighth_of_a_revolution);
  RUN_TEST(test_a_repeated_feed_moves_once_per_repeat);
  RUN_TEST(test_feeding_is_relative_to_where_the_tape_already_is);
  RUN_TEST(test_the_tape_only_ever_goes_one_way);
  RUN_TEST(test_feed_energizes_before_moving_and_releases_after);
  RUN_TEST(test_the_motor_is_never_left_energized);
  RUN_TEST(test_a_feed_of_nothing_still_releases_the_motor);
  RUN_TEST(test_deenergize_releases_the_motor);
  RUN_TEST(test_nothing_is_counted_before_the_first_feed);
  RUN_TEST(test_each_repeat_is_counted);
  RUN_TEST(test_the_count_carries_across_feeds);
  RUN_TEST(test_a_feed_of_nothing_counts_nothing);
  RUN_TEST(test_the_coils_are_given_time_to_come_up_before_the_first_move);
  RUN_TEST(test_a_stop_before_a_feed_leaves_the_motor_alone);
  RUN_TEST(test_a_stop_halts_the_feed_at_the_step_it_arrives_on);
  RUN_TEST(test_a_stop_skips_the_feeds_still_to_come);
  RUN_TEST(test_a_stopped_feed_still_releases_the_motor);
  RUN_TEST(test_a_feed_a_stop_cut_short_counts_as_a_whole_one);
  RUN_TEST(test_a_feed_that_finishes_before_a_stop_is_not_cut_short);
  return UNITY_END();
}
