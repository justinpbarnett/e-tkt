// Host-side tests for Feeder, driven through the StepperDriver seam.
//
// Feeder is small, but two of the things it does are easy to break and
// impossible to see: it must leave the motor de-energised after every feed,
// or the tape cannot be pulled and the driver cooks, and it must feed
// relative to wherever the tape already is rather than to an absolute
// position. A FakeStepper records the whole call sequence so both are
// assertions. Run with:  pio test -e native
#include <unity.h>

#include "Arduino.h"
#include "Configuration.h"
#include "FakeDrivers.h"
#include "Feeder.h"
#include "Logger.h"

// One feed is an eighth of a turn of the feed motor, in whichever direction
// the configuration says. Integer division, exactly as Feeder computes it.
static const long FEED_STEP = (FEED_MOTOR_STEPS_PER_REVOLUTION / 8);
static const long FEED_DIRECTION = REVERSE_FEED_STEPPER_DIRECTION ? 1 : -1;

static Logger* logger;
static FakeStepper* stepper;
static Feeder* feeder;

void setUp(void) {
  stubReset();
  logger = new Logger();
  stepper = new FakeStepper();
  feeder = new Feeder(logger, stepper);
}

void tearDown(void) {
  delete feeder;
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
  const std::vector<long> moves =
      stepper->valuesOf(StepperCall::RUN_TO_NEW_POSITION);
  TEST_ASSERT_EQUAL_INT(1, (int)moves.size());
  TEST_ASSERT_EQUAL_INT32(FEED_STEP * FEED_DIRECTION, moves[0]);
}

void test_a_repeated_feed_moves_once_per_repeat(void) {
  feeder->initialize();
  feeder->feed(3);
  const std::vector<long> moves =
      stepper->valuesOf(StepperCall::RUN_TO_NEW_POSITION);
  TEST_ASSERT_EQUAL_INT(3, (int)moves.size());
  TEST_ASSERT_EQUAL_INT32(FEED_STEP * FEED_DIRECTION * 3,
                          moves[moves.size() - 1]);
}

void test_feeding_is_relative_to_where_the_tape_already_is(void) {
  // Two separate feeds must land six eighths along, not one eighth twice.
  feeder->initialize();
  feeder->feed(3);
  feeder->feed(3);
  const std::vector<long> moves =
      stepper->valuesOf(StepperCall::RUN_TO_NEW_POSITION);
  TEST_ASSERT_EQUAL_INT(6, (int)moves.size());
  TEST_ASSERT_EQUAL_INT32(FEED_STEP * FEED_DIRECTION * 6,
                          moves[moves.size() - 1]);
}

void test_the_tape_only_ever_goes_one_way(void) {
  feeder->initialize();
  feeder->feed(5);
  const std::vector<long> moves =
      stepper->valuesOf(StepperCall::RUN_TO_NEW_POSITION);
  for (size_t i = 1; i < moves.size(); i++) {
    const long step = moves[i] - moves[i - 1];
    TEST_ASSERT_EQUAL_INT32(FEED_STEP * FEED_DIRECTION, step);
  }
}

void test_feed_energizes_before_moving_and_releases_after(void) {
  feeder->initialize();
  feeder->feed();
  const int enabled = stepper->firstIndexOf(StepperCall::ENABLE_OUTPUTS);
  const int moved = stepper->firstIndexOf(StepperCall::RUN_TO_NEW_POSITION);
  const int disabled = stepper->firstIndexOf(StepperCall::DISABLE_OUTPUTS);
  TEST_ASSERT_TRUE(enabled >= 0 && moved >= 0 && disabled >= 0);
  TEST_ASSERT_TRUE_MESSAGE(enabled < moved,
                           "the coils must be live before the motor is asked "
                           "to move");
  TEST_ASSERT_TRUE_MESSAGE(moved < disabled,
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
  TEST_ASSERT_EQUAL_INT(0, stepper->countOf(StepperCall::RUN_TO_NEW_POSITION));
  TEST_ASSERT_FALSE(stepper->energized);
}

void test_deenergize_releases_the_motor(void) {
  feeder->initialize();
  stepper->enableOutputs();
  feeder->deenergize();
  TEST_ASSERT_FALSE(stepper->energized);
}

// --- timing --------------------------------------------------------------

void test_the_coils_are_given_time_to_come_up_before_the_first_move(void) {
  // The 10ms after enableOutputs() is there so the driver has settled before
  // the first step arrives.
  feeder->initialize();
  feeder->feed();
  const std::vector<StepperCall>& calls = stepper->calls;
  const int enabled = stepper->firstIndexOf(StepperCall::ENABLE_OUTPUTS);
  const int moved = stepper->firstIndexOf(StepperCall::RUN_TO_NEW_POSITION);
  TEST_ASSERT_TRUE_MESSAGE(calls[moved].atMs > calls[enabled].atMs,
                           "the first step must not land on the same tick as "
                           "the enable");
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
  RUN_TEST(test_the_coils_are_given_time_to_come_up_before_the_first_move);
  return UNITY_END();
}
