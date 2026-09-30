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

#include <map>
#include <vector>

#include "Arduino.h"
#include "CharacterSet.h"
#include "Configuration.h"
#include "DaisyWheel.h"
#include "FakeDrivers.h"
#include "FakeMagnet.h"
#include "HallSwitch.h"
#include "Logger.h"
#include "Motion.h"
#include "PressGeometry.h"
#include "StopSignal.h"

// What runs while the wheel turns, as the tape does on the machine. This one
// only notes when it got a turn.
class Watching : public Background {
 public:
  std::vector<unsigned long> atUs;
  void keepGoing() override { this->atUs.push_back(micros()); }
  void finish() override {}
};

static FakeStepper* charStepper;
static FakeMagnet* magnet;

static Logger* logger;
static StopSignal* stopSignal;
static HallSwitch* hall;
static Watching* tape;
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
  tape = new Watching();
  daisywheel = new DaisyWheel(logger, hall, charStepper, stopSignal, tape);
  daisywheel->initialize();
}

// How far apart two bearings are, the short way round.
static long apart(long a, long b) {
  const long d = labs(a - b) % FakeMagnet::REVOLUTION;
  return d < FakeMagnet::REVOLUTION - d ? d : FakeMagnet::REVOLUTION - d;
}

void tearDown(void) {
  delete daisywheel;
  delete tape;
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

// --- the tape -------------------------------------------------------------

// The tape feeds while the wheel turns, and a motor steps only when it is
// kept going. So every loop and wait of a turn keeps the tape going: the
// step off the magnet, the search for it, the align, the turn to the slot
// and the waits after them. None keeps the tape waiting a millisecond, less
// than the time between two steps of a feed at its fastest.
void test_a_turn_keeps_the_tape_going_the_whole_way(void) {
  daisywheel->home(5);
  // Turned by hand onto the magnet, so homing starts by stepping off it.
  charStepper->shaft = 1000 + FakeMagnet::ARC / 2;
  tape->atUs.clear();
  const unsigned long began = micros();

  TEST_ASSERT_TRUE(daisywheel->move("Z", 5) == Turn::REACHED);

  const unsigned long ended = micros();
  TEST_ASSERT_FALSE(tape->atUs.empty());
  unsigned long longest = tape->atUs.front() - began;
  for (size_t i = 1; i < tape->atUs.size(); i++) {
    longest = max(longest, tape->atUs[i] - tape->atUs[i - 1]);
  }
  longest = max(longest, ended - tape->atUs.back());
  TEST_ASSERT_LESS_THAN_UINT32(1000, longest);
}

// --- how long it takes ----------------------------------------------------
// A job's estimate adds up the wheel's turns, so the wheel's own estimates
// are held to the wheel, on the virtual clock.

// Every character the wheel carries, the 0 and the 1 among them.
static std::vector<String> everyCharacter(void) {
  std::vector<String> all;
  for (std::map<String, int>::const_iterator it = CHARACTERS.begin();
       it != CHARACTERS.end(); ++it) {
    all.push_back(it->first);
  }
  return all;
}

void test_the_estimate_of_a_home_is_how_long_it_takes(void) {
  // Not 8 or 9: there this magnet's arc takes in the J, so a home from the J
  // steps off the magnet first, and the estimate leaves that out.
  const int aligns[] = {1, 5, 7};
  for (size_t a = 0; a < sizeof(aligns) / sizeof(aligns[0]); a++) {
    const int align = aligns[a];
    for (const String& from : everyCharacter()) {
      daisywheel->move(from, align);
      tape->atUs.clear();
      const unsigned long start = micros();
      TEST_ASSERT_TRUE(daisywheel->home(align) == Turn::REACHED);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(
          micros() - start, daisywheel->homeUs(from, align), from.c_str());
    }
  }
}

void test_a_wheel_that_has_lost_its_place_is_estimated_from_the_j(void) {
  TEST_ASSERT_EQUAL_UINT32(daisywheel->homeUs(CHAR_HOME_CHARACTER, 5),
                           daisywheel->homeUs("#", 5));
  TEST_ASSERT_EQUAL_UINT32(daisywheel->moveUs(CHAR_HOME_CHARACTER, "A", 5),
                           daisywheel->moveUs("#", "A", 5));
}

void test_the_estimate_of_a_turn_is_how_long_it_takes(void) {
  // From the slot homing finds soonest, and from the one it finds latest
  // but the J. From the I, the turn to the 1 is no turn at all.
  const char* froms[] = {"I", "K"};
  for (size_t f = 0; f < sizeof(froms) / sizeof(froms[0]); f++) {
    const String from = froms[f];
    for (const String& to : everyCharacter()) {
      daisywheel->move(from, 5);
      tape->atUs.clear();
      const unsigned long start = micros();
      TEST_ASSERT_TRUE(daisywheel->move(to, 5) == Turn::REACHED);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(micros() - start,
                                       daisywheel->moveUs(from, to, 5),
                                       (from + " to " + to).c_str());
    }
  }
}

void test_a_turn_to_a_character_the_wheel_lacks_takes_no_time(void) {
  TEST_ASSERT_EQUAL_UINT32(0, daisywheel->moveUs("A", "#", 5));
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
  RUN_TEST(test_a_turn_keeps_the_tape_going_the_whole_way);
  RUN_TEST(test_the_estimate_of_a_home_is_how_long_it_takes);
  RUN_TEST(test_a_wheel_that_has_lost_its_place_is_estimated_from_the_j);
  RUN_TEST(test_the_estimate_of_a_turn_is_how_long_it_takes);
  RUN_TEST(test_a_turn_to_a_character_the_wheel_lacks_takes_no_time);
  return UNITY_END();
}
