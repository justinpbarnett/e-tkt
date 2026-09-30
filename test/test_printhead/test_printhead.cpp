// Host-side tests for the printhead, Printhead: the daisy wheel and the
// press working together, through the calls a job makes on it.
//
// What comes out on the tape is which slot was under the press when it came
// down, how far in it went, and whether the tape was still then. So the
// wheel, the press and the feeder here are the real modules on fakes, with a
// FakeMagnet for the hall sensor to find and a StrokeLog that says, for each
// stroke, where the wheel stood and how deep the press went.
//
// Run with:  pio test -e native
#include <unity.h>

#include "Arduino.h"
#include "CharacterSet.h"
#include "Configuration.h"
#include "DaisyWheel.h"
#include "FakeDrivers.h"
#include "FakeMagnet.h"
#include "Feeder.h"
#include "HallSwitch.h"
#include "Light.h"
#include "Logger.h"
#include "Press.h"
#include "PressGeometry.h"
#include "Printhead.h"
#include "StopSignal.h"
#include "StrokeLog.h"

static FakeServo* pressServo;
static FakeStepper* charStepper;
static FakeStepper* feedStepper;
static FakeMagnet* magnet;
static StrokeLog* strokes;

static Logger* logger;
static StopSignal* stopSignal;
static Light* ledChar;
static Press* press;
static HallSwitch* hall;
static Feeder* feeder;
static DaisyWheel* daisywheel;
static Printhead* printhead;

void setUp(void) {
  stubReset();
  pressServo = new FakeServo();
  charStepper = new FakeStepper();
  feedStepper = new FakeStepper();
  // Anywhere but where the shaft starts, so the first home has to find it.
  magnet = new FakeMagnet(charStepper, 1000);
  magnet->install();
  strokes = new StrokeLog(pressServo, magnet);

  logger = new Logger();
  stopSignal = new StopSignal();
  ledChar = new Light(CHARACTER_LED_PIN, stopSignal);
  press = new Press(logger, SERVO_PIN, ledChar, pressServo);
  hall = new HallSwitch(logger, HALL_PIN);
  feeder = new Feeder(logger, feedStepper, stopSignal);
  daisywheel = new DaisyWheel(logger, hall, charStepper, stopSignal, feeder);
  printhead = new Printhead(logger, daisywheel, press, stopSignal, feeder);
  feeder->initialize();
  const Calibration saved = {5, 5};
  printhead->initialize(saved);
}

void tearDown(void) {
  delete printhead;
  delete daisywheel;
  delete feeder;
  delete hall;
  delete press;
  delete ledChar;
  delete stopSignal;
  delete logger;
  delete strokes;
  delete magnet;
  delete feedStepper;
  delete charStepper;
  delete pressServo;
}

// How deep a character goes at one force. PressGeometry works it out, and
// test_press_geometry pins that sum against numbers taken off machine 1.
static int depthAt(int force) {
  return pressPeakAngle(REST_ANGLE, STAMP_ANGLE, PRESS_BITE_AT_MAX_FORCE,
                        force);
}

// --- where the wheel is ---------------------------------------------------

// The home command finds the magnet and turns the home character, the J,
// under the press, at the job's align. It is the operator's check that the
// wheel knows where it is.
void test_homing_leaves_the_home_character_under_the_press(void) {
  const Calibration calibration = {9, 5};
  printhead->stamp(CHAR_HOME_CHARACTER, calibration);
  const long slotOfJ = strokes->strokes.back().bearing;
  printhead->turnTo("A", calibration);

  printhead->home(calibration);

  TEST_ASSERT_EQUAL_INT32(slotOfJ, magnet->bearing());
}

// A job ends by parking: the press up and the wheel let go, so neither
// motor sits hot between jobs. A wheel with its coils off turns by hand,
// and does when a label jams, so the next character finds home again
// rather than trusting where the wheel was left.
void test_a_parked_wheel_turned_by_hand_still_prints_in_the_right_slot(void) {
  const Calibration calibration = {5, 5};
  printhead->stamp("A", calibration);
  const long slotOfA = strokes->strokes.back().bearing;
  press->hold(STAMP_ANGLE);

  printhead->park();

  TEST_ASSERT_FALSE(charStepper->energized);
  TEST_ASSERT_EQUAL_INT(REST_ANGLE, pressServo->angles().back());
  charStepper->shaft += 500;
  printhead->stamp("A", calibration);
  TEST_ASSERT_EQUAL_INT32(slotOfA, strokes->strokes.back().bearing);
}

// --- a character ------------------------------------------------------------

// One character is one stroke of the press, as deep as the job's force. A
// second stroke on the same slot doubles the impression, and a stroke at
// the saved force when the job is trialling another is what the full test
// used to do to its cut.
void test_a_stamp_presses_the_character_once_at_the_jobs_force(void) {
  const Calibration calibration = {5, 7};

  printhead->stamp("A", calibration);

  TEST_ASSERT_EQUAL_INT(1, (int)strokes->strokes.size());
  TEST_ASSERT_EQUAL_INT(depthAt(7), strokes->strokes[0].deepest);
}

// A space is tape fed past with nothing pressed into it. The wheel stays
// where the last character left it, coils on, so a label like "A A" presses
// its second A without turning the wheel. A space sent to the wheel as a
// character let go of it, and every letter after a space homed again, a
// full turn for nothing.
void test_a_space_presses_nothing_and_leaves_the_wheel_where_it_was(void) {
  const Calibration calibration = {5, 5};
  printhead->stamp("A", calibration);
  const long shaft = charStepper->shaft;

  printhead->stamp(" ", calibration);
  printhead->stamp("A", calibration);

  TEST_ASSERT_EQUAL_INT32(shaft, charStepper->shaft);
  TEST_ASSERT_EQUAL_INT(2, (int)strokes->strokes.size());
}

// Nothing on the wheel prints a #, so the wheel goes nowhere and the press
// stays up. Pressing anyway would emboss whichever slot the wheel last
// stopped at. The panel and the HTTP boundary refuse such a label, so only a
// caller inside the device gets this far.
void test_a_character_the_wheel_does_not_carry_presses_nothing(void) {
  printhead->stamp("#", {5, 5});

  TEST_ASSERT_EQUAL_INT(0, (int)strokes->strokes.size());
}

// The stop can land in the moment between the wheel reaching the slot and
// the press starting down. The press stays up then too: a character pressed
// after the stop is one more on the tape than the operator let through.
void test_a_stop_as_the_wheel_reaches_the_slot_keeps_the_press_up(void) {
  const Calibration calibration = {5, 5};
  printhead->stamp("A", calibration);
  static long slotOfA;
  slotOfA = strokes->strokes.back().bearing;
  printhead->stamp("B", calibration);
  // The last step onto the A, after which the wheel does not move again.
  charStepper->afterStep = [] {
    if (magnet->bearing() == slotOfA && charStepper->distanceToGo() == 0) {
      stopSignal->raise(StopCause::OPERATOR);
    }
  };

  printhead->stamp("A", calibration);

  TEST_ASSERT_EQUAL_INT(2, (int)strokes->strokes.size());
  TEST_ASSERT_EQUAL_INT32(slotOfA, magnet->bearing());
}

// The move command turns the wheel to a slot so the operator can look at
// it, and presses nothing: it is how the align is checked by eye. So the
// slot it leaves under the press is the one a stamp of that character
// strikes.
void test_turning_to_a_slot_leaves_it_under_the_press(void) {
  const Calibration calibration = {5, 5};
  printhead->stamp("A", calibration);
  const long slotOfA = strokes->strokes.back().bearing;
  printhead->stamp("B", calibration);

  printhead->turnTo("A", calibration);

  TEST_ASSERT_EQUAL_INT32(slotOfA, magnet->bearing());
  TEST_ASSERT_EQUAL_INT(2, (int)strokes->strokes.size());
}

// A move that does not arrive leaves the wheel somewhere other than the
// slot the operator asked for, and the log is where they find out why. A
// stop is no surprise, since the operator asked for it, so that one is not
// logged.
void test_a_turn_that_does_not_arrive_says_so_unless_it_was_stopped(void) {
  printhead->turnTo("#", {5, 5});
  stopSignal->raise(StopCause::OPERATOR);
  printhead->turnTo("A", {5, 5});

  const String log = logger->recent();
  TEST_ASSERT_TRUE(log.indexOf("The wheel would not reach '#'") >= 0);
  TEST_ASSERT_TRUE(log.indexOf("The wheel would not reach 'A'") < 0);
}

// --- a lost wheel ---------------------------------------------------------

// A wheel that turned without finding its magnet cannot say which slot is
// under the press, so the job ends there, through the same stop as the stop
// button, and the stop says it was the wheel. Upstream logged the failure
// and pressed every character after it into whichever slot came round.
void test_a_lost_wheel_presses_nothing_and_stops_the_job(void) {
  magnet->present = false;

  printhead->stamp("A", {5, 5});

  TEST_ASSERT_EQUAL_INT(0, (int)strokes->strokes.size());
  TEST_ASSERT_TRUE(stopSignal->cause() == StopCause::LOST_WHEEL);
  TEST_ASSERT_TRUE(stopSignal->cutShort());
}

// Not only a character: every turn the printhead makes homes first, and
// each one that finds no magnet ends the job the same way. The home and move
// commands are nothing but the turn, so without this a lost wheel would end
// them as if they had worked.
void test_every_turn_of_a_lost_wheel_stops_the_job(void) {
  magnet->present = false;
  struct Turning {
    const char* name;
    void (*turn)();
  };
  const Turning turnings[] = {
      {"home", [] { printhead->home({5, 5}); }},
      {"turn to a slot", [] { printhead->turnTo("A", {5, 5}); }},
      {"cut", [] { printhead->cut({5, 5}); }},
      {"test press", [] { printhead->testPress({5, 5}); }},
  };

  for (size_t i = 0; i < sizeof(turnings) / sizeof(turnings[0]); i++) {
    stopSignal->clear();
    turnings[i].turn();
    TEST_ASSERT_TRUE_MESSAGE(stopSignal->cause() == StopCause::LOST_WHEEL,
                             turnings[i].name);
    TEST_ASSERT_TRUE_MESSAGE(stopSignal->cutShort(), turnings[i].name);
  }
  TEST_ASSERT_EQUAL_INT(0, (int)strokes->strokes.size());
}

// No job is running at boot, so there is none to end. A stop raised then
// would be up when the first job started, and end it before it had begun.
// The first character homes again, and finds out then.
void test_a_wheel_lost_at_boot_raises_no_stop(void) {
  magnet->present = false;

  printhead->initialize({5, 5});

  TEST_ASSERT_FALSE(stopSignal->raised());
}

// --- the tape -------------------------------------------------------------

// When the press first left rest, at or after servo call `from`, in
// milliseconds. 0 if it never did.
static unsigned long firstPressFrom(size_t from) {
  for (size_t i = from; i < pressServo->calls.size(); i++) {
    const ServoCall& call = pressServo->calls[i];
    if (call.kind == ServoCall::WRITE && call.value != REST_ANGLE) {
      return call.atMs;
    }
  }
  return 0;
}

// When the feed motor let go of the tape, at or after stepper call `from`,
// in milliseconds. 0 if it has not: the boot home alone takes longer than
// that, so no test here lets go of anything at 0.
static unsigned long letGoFrom(size_t from) {
  for (size_t i = from; i < feedStepper->calls.size(); i++) {
    const StepperCall& call = feedStepper->calls[i];
    if (call.kind == StepperCall::DISABLE_OUTPUTS) {
      return call.atMs;
    }
  }
  return 0;
}

// Nothing comes down on tape that is moving, since that drags the character
// along the label. Whatever presses waits for the feed under way to arrive
// and for the tape to settle once the motor lets go, even with the wheel
// already on the slot and nothing else to wait for.
void test_nothing_comes_down_on_tape_that_is_still_moving(void) {
  struct Pressing {
    const char* slot;
    void (*press)();
  };
  const Pressing pressings[] = {
      {"A", [] { printhead->stamp("A", {5, 5}); }},
      {CUT_CHARACTER, [] { printhead->cut({5, 5}); }},
      {"M", [] { printhead->testPress({5, 5}); }},
  };

  for (const Pressing& pressing : pressings) {
    printhead->turnTo(pressing.slot, {5, 5});
    const long tapeWasAt = feedStepper->currentPosition();
    const size_t feedFrom = feedStepper->calls.size();
    const size_t pressFrom = pressServo->calls.size();

    feeder->start(1);
    pressing.press();

    TEST_ASSERT_EQUAL_INT32_MESSAGE(
        STEPS_PER_FEED, labs(feedStepper->currentPosition() - tapeWasAt),
        pressing.slot);
    const unsigned long letGo = letGoFrom(feedFrom);
    const unsigned long pressed = firstPressFrom(pressFrom);
    TEST_ASSERT_TRUE_MESSAGE(letGo > 0, pressing.slot);
    TEST_ASSERT_TRUE_MESSAGE(pressed > 0, pressing.slot);
    TEST_ASSERT_TRUE_MESSAGE(pressed >= letGo + FEED_SETTLE_OFF_MS,
                             pressing.slot);
  }
}

// The tape feeds up to the next character while the wheel turns to it, so a
// character takes as long as the slower of the two rather than both.
void test_the_wheel_turns_to_the_next_character_while_the_tape_feeds(void) {
  const Calibration calibration = {5, 5};
  printhead->stamp("A", calibration);
  static bool together;
  together = false;
  charStepper->afterStep = [] {
    const long fed = labs(feedStepper->currentPosition());
    together = together || (fed > 0 && fed < STEPS_PER_FEED);
  };

  feeder->start(1);
  printhead->stamp("B", calibration);

  TEST_ASSERT_TRUE(together);
  TEST_ASSERT_EQUAL_INT(2, (int)strokes->strokes.size());
}

// The stop can come while the press waits for the tape. The press stays up
// then, as it does for a stop that comes while the wheel turns: it has not
// started down, so nothing of the character is on the tape.
void test_a_stop_while_the_press_waits_for_the_tape_keeps_it_up(void) {
  const Calibration calibration = {5, 5};
  printhead->turnTo("A", calibration);
  feedStepper->afterStep = [] { stopSignal->raise(StopCause::OPERATOR); };

  feeder->start(1);
  printhead->stamp("A", calibration);

  TEST_ASSERT_EQUAL_INT(0, (int)strokes->strokes.size());
  TEST_ASSERT_TRUE(stopSignal->cutShort());
}

// --- the cut ------------------------------------------------------------

// The cut is the cut mark pressed once, as hard as the job presses its
// characters. It used to press three times, and the tape still came out
// joined, so the label came off with scissors either way. Two of the three
// were time for nothing on every label.
void test_the_cut_presses_the_cut_mark_once_at_the_jobs_force(void) {
  printhead->cut({5, 7});

  TEST_ASSERT_EQUAL_INT(1, (int)strokes->strokes.size());
  TEST_ASSERT_EQUAL_INT(depthAt(7), strokes->strokes[0].deepest);
}

// The slot the cut comes down on is the cut mark, the blade on the wheel,
// and at the job's align like every other press of the job.
void test_the_cut_comes_down_on_the_cut_mark(void) {
  const Calibration calibration = {9, 5};
  printhead->turnTo(CUT_CHARACTER, calibration);
  const long cutMark = magnet->bearing();
  printhead->turnTo("A", calibration);

  printhead->cut(calibration);

  TEST_ASSERT_EQUAL_INT32(cutMark, strokes->strokes[0].bearing);
}

// --- the test press -----------------------------------------------------

// The align test presses the M slowly and lightly, and holds it, so the
// operator can see whether the press lands centred on the letter. It stays
// light whatever force the job carries: the calibration guide sends the
// operator here with the force wound up to 9, and a full bite held for
// seconds is a stalled servo, which strips its gears.
void test_the_test_press_is_light_and_slow_on_the_m_whatever_the_force(void) {
  const Calibration calibration = {5, 9};
  printhead->turnTo("M", calibration);
  const long slotOfM = magnet->bearing();
  printhead->turnTo("A", calibration);

  printhead->testPress(calibration);

  TEST_ASSERT_EQUAL_INT(1, (int)strokes->strokes.size());
  TEST_ASSERT_EQUAL_INT32(slotOfM, strokes->strokes[0].bearing);
  const int light = depthAt(CALIBRATION_VALUE_MIN);
  TEST_ASSERT_EQUAL_INT(light, strokes->strokes[0].deepest);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(PRESS_TEST_DWELL_MS,
                                      pressServo->longestHoldAt(light));
}

// --- how long it takes ----------------------------------------------------
// A job's estimate is these added up, so each is held to what it estimates,
// on the virtual clock.

// How long a character takes as a label prints it: its feed started, then
// the stamp, then whatever is left of the feed.
static unsigned long timeCharacter(const String& character, int feeds,
                                   const Calibration& calibration) {
  const unsigned long start = micros();
  feeder->start(feeds);
  printhead->stamp(character, calibration);
  feeder->finish();
  return micros() - start;
}

// The same for the cut, as a label ends with it.
static unsigned long timeCut(int feeds, const Calibration& calibration) {
  const unsigned long start = micros();
  feeder->start(feeds);
  printhead->cut(calibration);
  feeder->finish();
  return micros() - start;
}

// A character, or the cut mark for the cut, pressed with the wheel at
// `from` and `feeds` of tape feeding.
struct Stamping {
  const char* from;
  const char* character;
  int feeds;

  bool cutting() const { return String(this->character) == CUT_CHARACTER; }

  String name() const {
    return String(this->from) + " to '" + this->character + "' over " +
           this->feeds;
  }
};

// How long the estimate says a stamping takes, with tapeUs of feeding.
static unsigned long estimateOf(const Stamping& stamping, unsigned long tapeUs,
                                const Calibration& calibration) {
  String wheel = stamping.from;
  return stamping.cutting() ? printhead->cutUs(&wheel, tapeUs, calibration)
                            : printhead->stampUs(&wheel, stamping.character,
                                                 tapeUs, calibration);
}

// And how long it takes, from the wheel at rest at `from`.
static unsigned long timeOf(const Stamping& stamping,
                            const Calibration& calibration) {
  printhead->turnTo(stamping.from, calibration);
  return stamping.cutting()
             ? timeCut(stamping.feeds, calibration)
             : timeCharacter(stamping.character, stamping.feeds, calibration);
}

// A run homes the wheel before its first label, and the job before it
// parked the wheel, which lost its place. So the estimate counts the longest
// search there is: from the J, which sits just past the magnet.
void test_the_estimate_of_a_home_is_how_long_it_takes_from_the_j(void) {
  const Calibration calibration = {5, 5};
  printhead->turnTo(CHAR_HOME_CHARACTER, calibration);
  printhead->park();
  const unsigned long start = micros();
  printhead->home(calibration);
  TEST_ASSERT_EQUAL_UINT32(micros() - start, printhead->homeUs(calibration));
}

void test_no_parked_wheel_takes_longer_to_home_than_the_estimate(void) {
  const Calibration calibration = {5, 5};
  for (const auto& character : CHARACTERS) {
    printhead->turnTo(character.first, calibration);
    printhead->park();
    const unsigned long start = micros();
    printhead->home(calibration);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(printhead->homeUs(calibration),
                                             micros() - start,
                                             character.first.c_str());
  }
}

// Where the wheel turns for longer than the tape feeds, and where it does
// not turn at all.
void test_the_estimate_of_a_character_is_how_long_it_takes(void) {
  const Stamping stampings[] = {
      {"A", " ", 1}, {"A", " ", 2}, {"A", "A", 1},
      {"K", "I", 1}, {"J", "I", 1}, {"K", "@", 1},
  };
  const Calibration calibration = {5, 5};
  for (const Stamping& stamping : stampings) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        timeOf(stamping, calibration),
        estimateOf(stamping, feeder->feedUs(stamping.feeds), calibration),
        stamping.name().c_str());
  }
}

void test_the_estimate_of_a_cut_is_how_long_it_takes(void) {
  const Stamping stampings[] = {
      {"K", CUT_CHARACTER, 1},
      {CUT_CHARACTER, CUT_CHARACTER, 1},
  };
  const Calibration calibration = {5, 5};
  for (const Stamping& stamping : stampings) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        timeOf(stamping, calibration),
        estimateOf(stamping, feeder->feedUs(stamping.feeds), calibration),
        stamping.name().c_str());
  }
}

// While the wheel searches for its magnet it looks only every
// HOME_SWEEP_POLL_US, and so does the tape feeding meanwhile. Each step of
// the feed can come that much late, which the estimate does not count. It
// shows where the tape outlasts the turn.
void test_a_feed_that_outlasts_the_turn_runs_a_little_over_the_estimate(void) {
  const Stamping stampings[] = {
      {"J", "A", 2},
      {"I", "K", 1},
      {"A", CUT_CHARACTER, 3},
  };
  const Calibration calibration = {5, 5};
  for (const Stamping& stamping : stampings) {
    const String name = stamping.name();
    const unsigned long estimated =
        estimateOf(stamping, feeder->feedUs(stamping.feeds), calibration);
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(
        estimateOf(stamping, 0, calibration), estimated, name.c_str());

    const unsigned long taken = timeOf(stamping, calibration);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(estimated, taken, name.c_str());
    TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(
        estimated + stamping.feeds * STEPS_PER_FEED * HOME_SWEEP_POLL_US, taken,
        name.c_str());
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_homing_leaves_the_home_character_under_the_press);
  RUN_TEST(test_a_parked_wheel_turned_by_hand_still_prints_in_the_right_slot);
  RUN_TEST(test_a_stamp_presses_the_character_once_at_the_jobs_force);
  RUN_TEST(test_a_space_presses_nothing_and_leaves_the_wheel_where_it_was);
  RUN_TEST(test_a_character_the_wheel_does_not_carry_presses_nothing);
  RUN_TEST(test_a_stop_as_the_wheel_reaches_the_slot_keeps_the_press_up);
  RUN_TEST(test_turning_to_a_slot_leaves_it_under_the_press);
  RUN_TEST(test_a_turn_that_does_not_arrive_says_so_unless_it_was_stopped);
  RUN_TEST(test_a_lost_wheel_presses_nothing_and_stops_the_job);
  RUN_TEST(test_every_turn_of_a_lost_wheel_stops_the_job);
  RUN_TEST(test_a_wheel_lost_at_boot_raises_no_stop);
  RUN_TEST(test_nothing_comes_down_on_tape_that_is_still_moving);
  RUN_TEST(test_the_wheel_turns_to_the_next_character_while_the_tape_feeds);
  RUN_TEST(test_a_stop_while_the_press_waits_for_the_tape_keeps_it_up);
  RUN_TEST(test_the_cut_presses_the_cut_mark_once_at_the_jobs_force);
  RUN_TEST(test_the_cut_comes_down_on_the_cut_mark);
  RUN_TEST(test_the_test_press_is_light_and_slow_on_the_m_whatever_the_force);
  RUN_TEST(test_the_estimate_of_a_home_is_how_long_it_takes_from_the_j);
  RUN_TEST(test_no_parked_wheel_takes_longer_to_home_than_the_estimate);
  RUN_TEST(test_the_estimate_of_a_character_is_how_long_it_takes);
  RUN_TEST(test_the_estimate_of_a_cut_is_how_long_it_takes);
  RUN_TEST(test_a_feed_that_outlasts_the_turn_runs_a_little_over_the_estimate);
  return UNITY_END();
}
