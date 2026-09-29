// Host-side tests for the daisy wheel, DaisyWheel: finding home, and turning
// a slot to the press counted from there.
//
// The hall sensor reads a FakeMagnet on the character stepper's shaft, so it
// sees the magnet only while the wheel is turned to it, as the sensor on the
// machine does. Where the wheel ends up is read off the same magnet:
// FakeMagnet::bearing() is how far round from it the wheel stands.
//
// Run with:  pio test -e native
#include <stdlib.h>
#include <unity.h>

#include "Arduino.h"
#include "CharacterSet.h"
#include "Configuration.h"
#include "DaisyWheel.h"
#include "FakeDrivers.h"
#include "FakeMagnet.h"
#include "HallSwitch.h"
#include "Logger.h"
#include "PressGeometry.h"
#include "StopSignal.h"

static FakeStepper* charStepper;
static FakeMagnet* magnet;

static Logger* logger;
static StopSignal* stopSignal;
static HallSwitch* hall;
static DaisyWheel* daisywheel;

void setUp(void) {
  stubReset();
  charStepper = new FakeStepper();
  // Anywhere but where the shaft starts, so the first home has to find it.
  magnet = new FakeMagnet(charStepper, 1000);
  magnet->install();

  logger = new Logger();
  stopSignal = new StopSignal();
  hall = new HallSwitch(logger, HALL_PIN);
  daisywheel = new DaisyWheel(logger, hall, charStepper, stopSignal);
  daisywheel->initialize();
}

// How far apart two bearings are, the short way round.
static long apart(long a, long b) {
  const long d = labs(a - b) % FakeMagnet::REVOLUTION;
  return d < FakeMagnet::REVOLUTION - d ? d : FakeMagnet::REVOLUTION - d;
}

void tearDown(void) {
  delete daisywheel;
  delete hall;
  delete stopSignal;
  delete logger;
  delete magnet;
  delete charStepper;
}

// --- finding home ---------------------------------------------------------

// Every character is counted from home, so home has to be one place on the
// wheel however the wheel was left: by the last job, by a stop partway
// through a turn, or by a hand that turned it while the coils were off.
void test_homing_from_anywhere_ends_in_the_same_place(void) {
  daisywheel->home(5);
  const long home = magnet->bearing();

  const long turnedBy[] = {1, 19, 20, 400, 1600, 2900, 3199};
  for (size_t i = 0; i < sizeof(turnedBy) / sizeof(turnedBy[0]); i++) {
    charStepper->shaft += turnedBy[i];
    TEST_ASSERT_TRUE(daisywheel->home(5) == Turn::REACHED);
    TEST_ASSERT_EQUAL_INT32(home, magnet->bearing());
  }
}

// The align takes up where the sensor ended up on this machine: a nudge
// either way from where the magnet was found. Never as far as a slot, or a
// calibration would print the character beside the one it was asked for.
void test_the_align_nudges_home_by_less_than_a_slot(void) {
  daisywheel->home(CALIBRATION_VALUE_MIN);
  const long atMin = magnet->bearing();
  daisywheel->home(CALIBRATION_VALUE_MAX);
  const long atMax = magnet->bearing();

  TEST_ASSERT_TRUE(apart(atMin, atMax) > 0);
  TEST_ASSERT_TRUE(apart(atMin, atMax) <
                   FakeMagnet::REVOLUTION / WHEEL_SLOT_COUNT);
}

// A magnet that has come off the hub, or a sensor that has come unplugged,
// leaves nothing to find. Nothing counted from where the search gave up
// lands on the slot it was meant for, so the wheel says it is lost rather
// than carrying on as if it were home.
void test_a_wheel_with_no_magnet_to_find_is_lost(void) {
  magnet->present = false;

  TEST_ASSERT_TRUE(daisywheel->home(5) == Turn::LOST);
}

// A search that does not find the magnet has to end, and the job with it.
// Upstream's went on waiting for a trigger after the stepper had stopped,
// with nothing left that could change the reading, so a machine with no
// magnet hung on its first home.
void test_a_lost_wheel_gives_up_within_a_turn_and_a_half(void) {
  magnet->present = false;
  const long before = charStepper->shaft;

  daisywheel->home(5);

  TEST_ASSERT_TRUE(labs(charStepper->shaft - before) <=
                   FakeMagnet::REVOLUTION * 3 / 2);
}

// The stop button halts the wheel where it is, within a step, and the wheel
// says so. It is not lost: nothing was wrong with it.
void test_a_stop_while_homing_halts_the_wheel_within_a_step(void) {
  int steps = 0;
  long stoppedAt = 0;
  charStepper->afterStep = [&steps, &stoppedAt] {
    if (++steps == 100) {
      stopSignal->raise(StopCause::OPERATOR);
      stoppedAt = charStepper->shaft;
    }
  };

  TEST_ASSERT_TRUE(daisywheel->home(5) == Turn::STOPPED);
  TEST_ASSERT_EQUAL_INT32(stoppedAt, charStepper->shaft);
}

// --- turning to a slot ------------------------------------------------------

// Every turn to a slot homes first, so a wheel that has lost its magnet is
// found out at the next character, and goes no further. Counting a turn
// from where the search gave up would put some other slot under the press.
void test_a_turn_on_a_wheel_with_no_magnet_is_lost(void) {
  magnet->present = false;

  TEST_ASSERT_TRUE(daisywheel->move("A", 5) == Turn::LOST);
}

// Wherever the last character left the wheel, the next one lands in the
// same place, because the turn is counted from home and not from there.
void test_a_turn_puts_the_slot_under_the_press_from_any_slot(void) {
  TEST_ASSERT_TRUE(daisywheel->move("A", 5) == Turn::REACHED);
  const long slotOfA = magnet->bearing();

  const char* before[] = {"Z", "$", "@", "M"};
  for (size_t i = 0; i < sizeof(before) / sizeof(before[0]); i++) {
    daisywheel->move(before[i], 5);
    TEST_ASSERT_TRUE(daisywheel->move("A", 5) == Turn::REACHED);
    TEST_ASSERT_EQUAL_INT32(slotOfA, magnet->bearing());
  }
}

// Nothing on the wheel prints a '#', so the wheel does not turn for one,
// and lets go of its coils rather than holding still for a turn that will
// never come.
void test_a_turn_to_a_character_the_wheel_lacks_goes_nowhere(void) {
  const long before = charStepper->shaft;

  TEST_ASSERT_TRUE(daisywheel->move("#", 5) == Turn::NO_SLOT);
  TEST_ASSERT_EQUAL_INT32(before, charStepper->shaft);
  TEST_ASSERT_FALSE(charStepper->energized);
}

// The stop button halts a turn as it halts a home: within a step, and the
// wheel says it was stopped.
void test_a_stop_partway_through_a_turn_halts_the_wheel_within_a_step(void) {
  daisywheel->move("A", 5);
  long stoppedAt = 0;
  charStepper->afterStep = [&stoppedAt] {
    // Only the turn to the Z ever gets this close to where it is going: the
    // search for the magnet is sent a turn and a half out and finds it within
    // one, and the align is less than a slot.
    if (charStepper->distanceToGo() == -400) {
      stopSignal->raise(StopCause::OPERATOR);
      stoppedAt = charStepper->shaft;
    }
  };

  TEST_ASSERT_TRUE(daisywheel->move("Z", 5) == Turn::STOPPED);
  TEST_ASSERT_EQUAL_INT32(stoppedAt, charStepper->shaft);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_homing_from_anywhere_ends_in_the_same_place);
  RUN_TEST(test_the_align_nudges_home_by_less_than_a_slot);
  RUN_TEST(test_a_wheel_with_no_magnet_to_find_is_lost);
  RUN_TEST(test_a_lost_wheel_gives_up_within_a_turn_and_a_half);
  RUN_TEST(test_a_stop_while_homing_halts_the_wheel_within_a_step);
  RUN_TEST(test_a_turn_on_a_wheel_with_no_magnet_is_lost);
  RUN_TEST(test_a_turn_puts_the_slot_under_the_press_from_any_slot);
  RUN_TEST(test_a_turn_to_a_character_the_wheel_lacks_goes_nowhere);
  RUN_TEST(test_a_stop_partway_through_a_turn_halts_the_wheel_within_a_step);
  return UNITY_END();
}
