// Host-side tests for TimedFlag: whether something is so, and for how long it
// has been.
//
// The link supervisor asks that of seven things, from a try that is under way
// to an address that is on the screen. Each was a flag and a time kept side
// by side, and compared by hand wherever it was asked.
//
// Run with:  pio test -e native
#include <unity.h>

#include "TimedFlag.h"

void setUp(void) {}
void tearDown(void) {}

void test_a_flag_never_set_is_so_for_no_length_of_time(void) {
  // A wait that has not begun is not over, and is not still running either.
  TimedFlag flag;
  TEST_ASSERT_FALSE(flag.isSet());
  TEST_ASSERT_FALSE(flag.forLessThan(5000, 1000));
  TEST_ASSERT_FALSE(flag.forAtLeast(5000, 1000));
  TEST_ASSERT_FALSE(flag.forAtLeast(5000, 0));
}

void test_a_set_flag_says_how_long_it_has_been_so(void) {
  TimedFlag flag;
  flag.set(10000);
  TEST_ASSERT_TRUE(flag.isSet());
  TEST_ASSERT_TRUE(flag.forLessThan(10999, 1000));
  TEST_ASSERT_FALSE(flag.forAtLeast(10999, 1000));
  // The length itself counts as reached.
  TEST_ASSERT_FALSE(flag.forLessThan(11000, 1000));
  TEST_ASSERT_TRUE(flag.forAtLeast(11000, 1000));
}

void test_a_flag_set_again_counts_from_the_later_moment(void) {
  // A change made while another waits to be followed starts the wait again.
  TimedFlag flag;
  flag.set(10000);
  flag.set(10800);
  TEST_ASSERT_TRUE(flag.forLessThan(11500, 1000));
  TEST_ASSERT_TRUE(flag.forAtLeast(11800, 1000));
}

void test_a_cleared_flag_is_so_for_no_length_of_time(void) {
  TimedFlag flag;
  flag.set(10000);
  flag.clear();
  TEST_ASSERT_FALSE(flag.isSet());
  TEST_ASSERT_FALSE(flag.forLessThan(10500, 1000));
  TEST_ASSERT_FALSE(flag.forAtLeast(12000, 1000));
}

void test_the_count_is_right_across_the_clock_starting_over(void) {
  // millis() starts over every seven weeks, and a machine can be left on for
  // longer. Set 256 ms before that and asked 256 ms after it, the flag has
  // been so for 512 ms.
  TimedFlag flag;
  flag.set(0xFFFFFF00u);
  TEST_ASSERT_TRUE(flag.forLessThan(0x00000100u, 513));
  TEST_ASSERT_TRUE(flag.forAtLeast(0x00000100u, 512));
  TEST_ASSERT_FALSE(flag.forAtLeast(0x00000100u, 513));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_flag_never_set_is_so_for_no_length_of_time);
  RUN_TEST(test_a_set_flag_says_how_long_it_has_been_so);
  RUN_TEST(test_a_flag_set_again_counts_from_the_later_moment);
  RUN_TEST(test_a_cleared_flag_is_so_for_no_length_of_time);
  RUN_TEST(test_the_count_is_right_across_the_clock_starting_over);
  return UNITY_END();
}
